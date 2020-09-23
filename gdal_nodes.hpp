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
    add_param(ParamInt(layer_id, "layer_id", "Layer ID"));

    if (GDALGetDriverCount() == 0)
      GDALAllRegister();
  }
  void process();
};

class OGRWriterNode : public Node
{
  int epsg = 7415;
  std::string filepath_ = "out";
  std::string gdaldriver_ = "GPKG";
  std::string layername_ = "geom";
  bool overwrite_ = false;
  bool append_ = false;
  bool require_attributes_ = false;

  vec1s key_options;
  StrMap output_attribute_names;

  OGRPolygon create_polygon(const LinearRing& lr);

public:
  using Node::Node;
  void init()
  {
    add_vector_input("geometries", {typeid(LineString), typeid(LinearRing), typeid(TriangleCollection), typeid(Mesh)});
    add_poly_input("attributes", {typeid(bool), typeid(int), typeid(float), typeid(std::string)}, false);

    add_param(ParamPath(filepath_, "filepath", "File path"));
    add_param(ParamInt(epsg, "epsg", "EPSG"));
    add_param(ParamString(gdaldriver_, "gdaldriver", "GDAL driver (format)"));
    add_param(ParamString(layername_, "layername", "Layer name"));
    add_param(ParamBool(overwrite_, "overwrite", "Overwrite dataset if it exists"));
    add_param(ParamBool(append_, "append", "Append to the data set?"));
    add_param(ParamBool(require_attributes_, "require_attributes", "Only run when attributes input is connected"));
    add_param(ParamStrMap(output_attribute_names, key_options, "output_attribute_names", "Output attribute names"));

    if (GDALGetDriverCount() == 0)
      GDALAllRegister();
  }
  bool inputs_valid() {
    if (require_attributes_) {
      return vector_input("geometries").has_data() && poly_input("attributes").has_data();
    } else {
      return vector_input("geometries").has_data();
    }
  }

  void process();

  void on_receive(gfMultiFeatureInputTerminal& it);
};

class OGRPostGISWriterNode : public Node
{
  int epsg = 7415;
  std::string conn_string_ = "out";
  std::string gdaldriver_ = "PostgreSQL";
  std::string layername_ = "geom";
  bool overwrite_ = false;
  // bool append_ = false;
  int transaction_batch_size_ = 1000;

  vec1s key_options;
  StrMap output_attribute_names;

  OGRPolygon create_polygon(const LinearRing& lr);

public:
  using Node::Node;
  void init()
  {
    add_vector_input("geometries", {typeid(LineString), typeid(LinearRing), typeid(std::vector<TriangleCollection>), typeid(Mesh)});
    add_poly_input("attributes", {typeid(bool), typeid(int), typeid(float), typeid(std::string)}, false);

    add_param(ParamPath(conn_string_, "filepath", "Connection string"));
    add_param(ParamInt(epsg, "epsg", "EPSG"));
    add_param(ParamInt(transaction_batch_size_, "transaction_batch_size_", "Trnasaction batch size"));
    // add_param(ParamString(gdaldriver_, "gdaldriver", "GDAL driver (format)"));
    add_param(ParamString(layername_, "layername", "Layer name"));
    add_param(ParamBool(overwrite_, "overwrite", "Overwrite dataset if it exists"));
    // add_param(ParamBool(append_, "append", "Append to the data set?"));
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

} // namespace geoflow::nodes::gdal
