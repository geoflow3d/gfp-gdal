#include <geoflow/geoflow.hpp>

#include <ogrsf_frmts.h>

namespace geoflow::nodes::gdal
{

class OGRLoaderNode : public Node
{
  int layer_count = 0;
  int layer_id = 0;
  float base_elevation = 0;

  std::string filepath = "";

  std::string geometry_type_name;
  OGRwkbGeometryType geometry_type;

  void push_attributes(OGRFeature &poFeature);

public:
  using Node::Node;
  void init()
  {
    add_vector_output("line_strings", typeid(LineString));
    add_vector_output("linear_rings", typeid(LinearRing));

    add_poly_output("attributes", {typeid(bool), typeid(int), typeid(float), typeid(std::string)});

    add_param(ParamPath(filepath, "filepath", "File path"));
    add_param(ParamFloat(base_elevation, "base_elevation", "Base elevation"));
    // add_param(ParamInt(layer_id, "layer_id", "Layer ID"));

    if (GDALGetDriverCount() == 0)
      GDALAllRegister();
  }
  void process();
};

class OGRWriterNode : public Node
{
  int epsg = 7415;
  std::string filepath = "out";
  std::string gdaldriver = "GPKG";
  std::string layername = "geom";
  bool overwrite = false;
  bool append = false;

  vec1s key_options;
  StrMap output_attribute_names;

  OGRPolygon create_polygon(const LinearRing& lr);

public:
  using Node::Node;
  void init()
  {
    add_vector_input("geometries", {typeid(LineString), typeid(LinearRing), typeid(TriangleCollection), typeid(Mesh)});
    add_poly_input("attributes", {typeid(bool), typeid(int), typeid(float), typeid(std::string)}, false);

    add_param(ParamPath(filepath, "filepath", "File path"));
    add_param(ParamInt(epsg, "epsg", "EPSG"));
    add_param(ParamString(gdaldriver, "gdaldriver", "GDAL driver (format)"));
    add_param(ParamString(layername, "layername", "Layer name"));
    add_param(ParamBool(overwrite, "overwrite", "Overwrite dataset if it exists"));
    add_param(ParamBool(append, "append", "Append to the data set?"));
    add_param(ParamStrMap(output_attribute_names, key_options, "output_attribute_names", "Output attribute names"));

    if (GDALGetDriverCount() == 0)
      GDALAllRegister();
  }
  void process();

  void on_receive(gfMultiFeatureInputTerminal& it);
};

class CSVLoaderNode : public Node
{
  std::string filepath = "out";
  int thin_nth = 5;

public:
  using Node::Node;
  void init()
  {
    add_output("points", typeid(PointCollection));

    add_param(ParamPath(filepath, "filepath", "File path"));
    add_param(ParamBoundedInt(thin_nth, 0, 100, "thin_nth", "Thin factor"));
  }
  void process();
};

class CSVWriterNode : public Node
{
  std::string filepath = "out";

public:
  using Node::Node;
  void init()
  {
    add_input("points", typeid(PointCollection));
    add_input("distances", typeid(vec1f));

    add_param(ParamPath(filepath, "filepath", "File path"));
  }
  void process();
};

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

class PolygonUnionNode : public Node
{
public:
  using Node::Node;
  void init()
  {
    add_vector_input("polygons", typeid(LinearRing));
    add_vector_output("polygons", typeid(LinearRing));
    add_vector_output("holes", typeid(LinearRing));
  }
  void process();
};
} // namespace geoflow::nodes::gdal
