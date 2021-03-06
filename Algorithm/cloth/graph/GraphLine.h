#pragma once

#include "AbstractGraphCurve.h"
#include "ldpMat\ldp_basic_vec.h"

namespace ldp
{
	class GraphLine : public AbstractGraphCurve
	{
	public:
		GraphLine();
		GraphLine(const std::vector<GraphPoint*>& pts);

		virtual Float2 getPointByParam(float t)const;
		virtual Type getType()const { return TypeGraphLine; }
	protected:
		virtual float calcLength()const;
	private:
		
	};
}