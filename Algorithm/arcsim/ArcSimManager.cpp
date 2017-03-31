#include "ArcSimManager.h"
#include "adaptiveCloth\conf.hpp"
#include "adaptiveCloth\separateobs.hpp"
#include "adaptiveCloth\io.hpp"
#include "ldputil.h"
#include "Renderable\ObjMesh.h"
#include "cloth\clothManager.h"
namespace arcsim
{
	const static double g_num_edge_sample_angle_thre = 15 * ldp::PI_D / 180.;
	const static double g_num_edge_sample_dist_thre = 0.01;

	ArcSimManager::ArcSimManager()
	{
		m_bodyMesh.reset(new ObjMesh());
		m_clothMesh.reset(new ObjMesh());
		m_timeStamp.reset(new ldp::TimeStamp());
		m_threadMutex.reset(new std::mutex());
		m_timeStamp->Prefix("arcsim");
	}

	ArcSimManager::~ArcSimManager()
	{
		if (m_threadLoop)
		{
			if (m_threadLoop->joinable())
				m_threadLoop->join();
		}
	}

	void ArcSimManager::clear()
	{
		stop_simulate_loop_otherthread();
		if (m_sim.get())
		{
			for (auto& h : m_sim->handles)
			{
				delete h;
			}
			for (auto& c : m_sim->cloths)
			{
				delete_mesh(c.mesh);
				for (auto m : c.materials)
					delete m;
			}
			for (auto& m : m_sim->morphs)
			{
				for (auto& s : m.targets)
					delete_mesh(s);
			}
			for (auto& o : m_sim->obstacles)
			{
				delete_mesh(o.base_mesh);
				delete_mesh(o.curr_state_mesh);
			}
			m_sim.reset((Simulation*)nullptr);
		}
	}

	void ArcSimManager::loadFromJsonConfig(std::string bodyMeshFileName)
	{
		m_timeStamp->Reset();

		clear();
		m_sim.reset(new Simulation);
		load_json(bodyMeshFileName, *m_sim.get());
		m_timeStamp->Stamp("json loaded");

		prepare(*m_sim);
		m_needUpdateMesh = true;
		updateMesh();
	}

	static void objMesh2arcMesh(Mesh& mesh, const ObjMesh& omesh)
	{
		delete_mesh(mesh);

		for (const auto& v : omesh.vertex_list)
			mesh.add(new Node(Vec3(v[0], v[1], v[2])));
		for (const auto& v : omesh.vertex_texture_list)
			mesh.add(new Vert(Vec3(v[0], v[1], v[2])));
		for (const auto& f : omesh.face_list)
		{
			std::vector<Vert*> verts;
			std::vector<Node*> nodes;		
			for (int k = 0; k < 3; k++)
			{
				nodes.push_back(mesh.nodes[f.vertex_index[k]]);
				if (omesh.vertex_texture_list.size())
					verts.push_back(mesh.verts[f.texture_index[k]]);
				else
				{
					verts.push_back(new Vert(project<2>(nodes.back()->x),
						nodes.back()->label));
					mesh.add(verts.back());
				}
			}
			for (int v = 0; v < verts.size(); v++)
				connect(verts[v], nodes[v]);
			mesh.add(new Face(verts[0], verts[1], verts[2]));
		}
	}

	void ArcSimManager::loadFromClothManager(ldp::ClothManager* clothManager)
	{
		m_timeStamp->Reset();

		clear();
		m_sim.reset(new Simulation);
		load_json("data/arcsim/default.json", *m_sim.get());
		m_timeStamp->Stamp("json loaded");

		const ObjMesh* bodyMesh = clothManager->bodyMesh();
		ObjMesh clothMesh;
		clothManager->exportClothsMerged(clothMesh, true);
		objMesh2arcMesh(m_sim->cloths[0].mesh, clothMesh);
		objMesh2arcMesh(m_sim->obstacles[0].base_mesh, *bodyMesh);
		m_sim->obstacles[0].curr_state_mesh = deep_copy(m_sim->obstacles[0].base_mesh);
		prepare(*m_sim);
		separate_obstacles(m_sim->obstacle_meshes, m_sim->cloth_meshes);
		m_timeStamp->Stamp("obstacles seperated");
		m_needUpdateMesh = true;
		updateMesh();
		m_timeStamp->Stamp("cloth manager loaded");
	}

	void ArcSimManager::reset()
	{
		if (m_sim == nullptr)
			throw std::exception("ArcSimManager::reset: nullptr exception");
		// prepare for simulation
		prepare(*m_sim);
		m_timeStamp->Stamp("prepared");
		separate_obstacles(m_sim->obstacle_meshes, m_sim->cloth_meshes);
		m_timeStamp->Stamp("obstacles seperated");
		relax_initial_state(*m_sim);
		m_timeStamp->Stamp("initial state relaxed\n");
		m_needUpdateMesh = true;
		updateMesh();
		m_timeStamp->Stamp("obj mesh updated");
	}

	void ArcSimManager::saveCurrentCloth(std::string fullname)
	{
		if (m_sim.get() == nullptr)
			return;
		std::string path, name, ext;
		ldp::fileparts(fullname, path, name, ext);
		for (size_t i = 0; i < m_sim->cloths.size(); i++)
		{
			const auto& cloth = m_sim->cloths[i];
			std::string nm = fullname;
			if (m_sim->cloths.size() > 1)
				nm = ldp::fullfile(path, name + "_" + std::to_string(i) + ext);
			arcsim::save_obj(cloth.mesh, nm);
		} // end for cloth
	}

	void ArcSimManager::simulate(int nsteps)
	{
		if (m_sim.get() == nullptr)
			return;
		for (int k = 0; k < nsteps; k++)
			advance_step(*m_sim);
		m_needUpdateMesh = true;
		updateMesh();
		m_timeStamp->Stamp("step, time=%f/%f, phys=%f, collision=%f",
			m_sim->time, m_sim->end_time,
			m_sim->timers[Simulation::Physics].last,
			m_sim->timers[Simulation::Collision].last);
	}

	void ArcSimManager::convertToObj(const std::vector<arcsim::Mesh*>& meshes, ObjMesh& omesh)
	{
		omesh.clear();
		int t_cnt = 0, v_cnt = 0;
		for (const auto& mesh : meshes)
		{
			for (const auto& tri : mesh->faces)
			{
				ObjMesh::obj_face f;
				f.material_index = -1;
				f.vertex_count = 3;
				for (int k = 0; k < 3; k++)
				{
					f.vertex_index[k] = tri->v[k]->node->index + v_cnt;
					f.texture_index[k] = tri->v[k]->index + t_cnt;
				}
				omesh.face_list.push_back(f);
			}
			for (const auto& tex : mesh->verts)
				omesh.vertex_texture_list.push_back(ldp::Float2(tex->u[0], tex->u[1]));
			for (const auto& node : mesh->nodes)
				omesh.vertex_list.push_back(ldp::Float3(node->x[0], node->x[1], node->x[2]));
			t_cnt += (int)mesh->verts.size();
			v_cnt += (int)mesh->nodes.size();
		}
		omesh.updateNormals();
		omesh.updateBoundingBox();
	}

	void ArcSimManager::calcBoundingBox(float bmin[3], float bmax[3])
	{
		for (int k = 0; k < 3; k++)
		{
			bmin[k] = FLT_MAX;
			bmax[k] = -FLT_MAX;
		}
		if (m_sim == nullptr)
			return;
		
		for (int k = 0; k < 3; k++)
		{
			bmin[k] = m_bodyMesh->boundingBox[0][k];
			bmax[k] = m_bodyMesh->boundingBox[1][k];
		}
	}

	bool ArcSimManager::updateMesh()
	{
		if (!m_needUpdateMesh)
			return false;
		if (m_sim == nullptr)
			return false;
		m_threadMutex->lock();

		convertToObj(m_sim->obstacle_meshes, *m_bodyMesh);
		convertToObj(m_sim->cloth_meshes, *m_clothMesh);

		// add a different material to better render clothes
		ObjMesh::obj_material material = ObjMesh::default_material;
		material.diff = ldp::Float3(0.5, 0.7, 0.9);
		m_clothMesh->material_list.clear();
		m_clothMesh->material_list.push_back(material);
		for (auto& f : m_clothMesh->face_list)
			f.material_index = 0;

		m_needUpdateMesh = false;
		m_threadMutex->unlock();
		return true;
	}

	void ArcSimManager::start_simulate_loop_otherthread()
	{
		m_timeStamp->Reset();

		stop_simulate_loop_otherthread();
		reset();
		m_threadLoop.reset(new std::thread(simulate_thread_loop, this));
	}

	void ArcSimManager::stop_simulate_loop_otherthread()
	{
		// check existed thread
		if (m_threadLoop)
		{
			if (m_threadLoop->joinable())
				m_threadLoop->join();
		}
	}

	void simulate_thread_loop(ArcSimManager* threadData)
	{
		Simulation* sim = threadData->getSimulator();
		while (sim->time < sim->end_time)
		{
			advance_step(*sim);
			threadData->m_threadMutex->lock();
			threadData->m_needUpdateMesh = true;
			threadData->m_threadMutex->unlock();
			threadData->m_timeStamp->Stamp("%.3f/%.1f, pr=%.3f, ps=%.3f, co=%.3f, sl=%.3f, pl=%.3f",
				sim->time, sim->end_time,
				sim->timers[Simulation::Proximity].last,
				sim->timers[Simulation::Physics].last,
				sim->timers[Simulation::Collision].last,
				sim->timers[Simulation::StrainLimiting].last,
				sim->timers[Simulation::Plasticity].last);
		} // end while sim->time
	}
}