#include "gdal_nodes.hpp"
#include "geos_nodes.hpp"

using namespace geoflow::nodes::gdal;
using namespace geoflow::nodes::gfp_geos;

void register_nodes(geoflow::NodeRegister &node_register)
{
  node_register.register_node<OGRLoaderNode>("OGRLoader");
  node_register.register_node<OGRWriterNode>("OGRWriter");

  node_register.register_node<GDALWriterNode>("GDALWriter");
  node_register.register_node<GDALReaderNode>("GDALReader");

  node_register.register_node<CSVLoaderNode>("CSVLoader");
  node_register.register_node<CSVWriterNode>("CSVWriter");
  
  node_register.register_node<GEOSMergeLinesNode>("GEOSMergeLines");
  node_register.register_node<PolygonBufferGEOSNode>("PolygonBufferGEOS");
  node_register.register_node<PolygonSimplifyGEOSNode>("PolygonSimplifyGEOS");
  // node_register.register_node<PolygonOrientNode>("PolygonOrient");
}