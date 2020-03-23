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

    add_param("filepath", ParamPath(filepath, "File path"));
    add_param("base_elevation", ParamFloat(base_elevation, "Base elevation"));
    // add_param("layer_id", ParamInt(layer_id, "Layer ID"));

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
  std::string lco = "";
  bool append = false;

public:
  using Node::Node;
  void init()
  {
    add_vector_input("geometries", {typeid(LineString), typeid(LinearRing), typeid(TriangleCollection)});
    add_poly_input("attributes", {typeid(bool), typeid(int), typeid(float), typeid(std::string)}, false);

    add_param("filepath", ParamPath(filepath, "File path"));
    add_param("epsg", ParamInt(epsg, "EPSG"));
    add_param("gdaldriver", ParamString(gdaldriver, "GDAL driver (format)"));
    add_param("layername", ParamString(layername, "Layer name"));
    add_param("lco", ParamString(lco, "Layer creation options (comma separated)"));
    add_param("append", ParamBool(append, "Append to the data set?"));
  }
  void process();
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

    add_param("filepath", ParamPath(filepath, "File path"));
    add_param("thin_nth", ParamBoundedInt(thin_nth, 0, 100, "Thin factor"));
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

    add_param("filepath", ParamPath(filepath, "File path"));
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
