#include <geoflow/geoflow.hpp>

namespace geoflow::nodes::gfp_geos
{

class PolygonSimplifyGEOSNode:public Node {
  public:
  using Node::Node;
  float tolerance = 0.01;

  void init() {
    add_vector_input("polygons", typeid(LinearRing));
    add_vector_output("simplified_polygons", typeid(LinearRing));

    add_param(ParamBoundedFloat(tolerance, 0, 10, "tolerance",  "tolerance"));   
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

// class GEOSMergeLinesNode : public Node
// {
// public:
//   using Node::Node;
//   void init()
//   {
//     add_input("lines", typeid(LineStringCollection));
//     add_output("lines", typeid(LineStringCollection));
//   }
//   void process();
// };

} // namespace geoflow::nodes::gdal
