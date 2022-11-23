// This file is part of gfp-gdal
// Copyright (C) 2018-2022 Ravi Peters

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
#include <geoflow/geoflow.hpp>

namespace geoflow::nodes::gfp_geos
{

class PolygonSimplifyGEOSNode:public Node {
  public:
  using Node::Node;
  float tolerance = 0.01;
  bool output_failures = true;
  bool orient_after_simplify = true;

  void init() {
    add_vector_input("polygons", typeid(LinearRing));
    add_vector_output("simplified_polygons", typeid(LinearRing));

    add_param(ParamBoundedFloat(tolerance, 0, 10, "tolerance",  "tolerance"));   
    add_param(ParamBool(output_failures, "output_failures",  "output polygons that could not be simplified"));   
    add_param(ParamBool(orient_after_simplify, "orient_after_simplify",  "Orient polygons after simplification"));   
  }
  void process();
};

class PolygonBufferGEOSNode:public Node {
  public:
  using Node::Node;
  float offset = 4;

  void init() {
    add_vector_input("polygons", typeid(LinearRing));
    add_vector_output("offset_polygons", typeid(LinearRing));

    add_param(ParamBoundedFloat(offset, -10, 10, "offset",  "offset"));   
  }
  void process();
};

// class PolygonOrientNode:public Node {
//   public:
//   using Node::Node;
//   bool make_ccw = true;

//   void init() {
//     add_vector_input("polygons", typeid(LinearRing));
//     add_vector_output("offset_polygons", typeid(LinearRing));

//     add_param(ParamBool(make_ccw, "make_ccw",  "orient CCW (CW if false)"));   
//   }
//   void process();
// };

class GEOSMergeLinesNode : public Node
{
public:
  using Node::Node;
  void init()
  {
    add_input("lines", typeid(LineStringCollection));
    add_output("lines", typeid(LineStringCollection));
  }
  void process();
};

} // namespace geoflow::nodes::gdal
