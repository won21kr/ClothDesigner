#include "ArcsimWindow.h"
#include "global_data_holder.h"
#include "cloth\clothManager.h"
#include "cloth\SmplManager.h"
#include "cloth\TransformInfo.h"
#include "arcsim\ArcSimManager.h"
#include "Renderable\ObjMesh.h"
#include "tinyxml\tinyxml.h"
#include <QFileInfo>
#include <random>
#include <fstream>


ArcsimWindow::ArcsimWindow(QWidget *parent)
: QMainWindow(parent)
{
	ui.setupUi(this);
	m_updateMeshView_timer = startTimer(30);
}

ArcsimWindow::~ArcsimWindow()
{

}

void ArcsimWindow::closeEvent(QCloseEvent* ev)
{
	if (g_dataholder.m_arcsimManager)
		g_dataholder.m_arcsimManager->stop_simulate_loop_otherthread();
}

void ArcsimWindow::init()
{
	ui.widget->init(g_dataholder.m_arcsimManager.get());
}

void ArcsimWindow::timerEvent(QTimerEvent* ev)
{
	if (ev->timerId() == m_updateMeshView_timer)
	{
		if (g_dataholder.m_arcsimManager->updateMesh())
			ui.widget->updateGL();
		setWindowTitle(g_dataholder.m_arcsimManager->getIterInfo().c_str());
	}
}

void ArcsimWindow::on_actionLoad_conf_triggered()
{
	try
	{
		QString name = QFileDialog::getOpenFileName(this, "open arcsim config", "data/arcsim", "*.json");
		if (name.isEmpty())
			return;
		g_dataholder.m_arcsimManager->loadFromJsonConfig(name.toStdString());
		ui.widget->resetCamera();
		ui.widget->updateGL();
	}
	catch (std::exception e)
	{
		std::cout << e.what() << std::endl;
	}
}

void ArcsimWindow::on_actionSave_cloth_triggered()
{
	try
	{
		QString name = QFileDialog::getSaveFileName(this, "save cloth meshes", "data/arcsim", "*.obj");
		if (name.isEmpty())
			return;
		if (!name.toLower().endsWith(".obj"))
			name.append(".obj");
		g_dataholder.m_arcsimManager->getClothMesh()->saveObj(name.toStdString().c_str());
	}
	catch (std::exception e)
	{
		std::cout << e.what() << std::endl;
	}
}

void ArcsimWindow::on_pbFromCloths_clicked()
{
	try
	{
		g_dataholder.m_arcsimManager->loadFromClothManager(g_dataholder.m_clothManager.get());
		ui.widget->resetCamera();
		ui.widget->updateGL();
	}
	catch (std::exception e)
	{
		std::cout << e.what() << std::endl;
	}
}

void ArcsimWindow::on_pbStartSimulation_clicked()
{
	try
	{
		g_dataholder.m_arcsimManager->start_simulate_loop_otherthread();
	}
	catch (std::exception e)
	{
		std::cout << e.what() << std::endl;
	}
}