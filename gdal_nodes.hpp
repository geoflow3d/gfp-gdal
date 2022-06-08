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

  void push_attributes(OGRFeature &poFeature, std::unordered_map<std::string,int>& fieldNameMap);

public:
  using Node::Node;
  void init()
  {
    add_vector_output("line_strings", typeid(LineString));
    add_vector_output("linear_rings", typeid(LinearRing));

    add_vector_output("area", typeid(float));
    add_vector_output("is_valid", typeid(bool));

    add_poly_output("attributes", {typeid(bool), typeid(int), typeid(float), typeid(std::string), typeid(Date), typeid(Time), typeid(DateTime)});

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
  std::string conn_string_ = "out";
  std::string gdaldriver_ = "GPKG";
  std::string layername_ = "geom";
  // bool overwrite_dataset_ = false;
  bool overwrite_layer_ = false;
  bool create_directories_ = true;
  bool require_attributes_ = true;
  int transaction_batch_size_ = 1000;

  vec1s key_options;
  StrMap output_attribute_names;

  OGRPolygon create_polygon(const LinearRing& lr);

public:
  using Node::Node;
  void init()
  {
    add_vector_input("geometries", {typeid(LineString), typeid(LinearRing), typeid(std::vector<TriangleCollection>), typeid(MultiTriangleCollection), typeid(Mesh), typeid(std::unordered_map<int, Mesh>)});
    add_poly_input("attributes", {typeid(bool), typeid(int), typeid(float), typeid(std::string), typeid(Date), typeid(Time), typeid(DateTime)}, false);

    add_param(ParamPath(conn_string_, "filepath", "Filepath or database connection string"));
    add_param(ParamInt(epsg, "epsg", "EPSG"));
    add_param(ParamInt(transaction_batch_size_, "transaction_batch_size_", "Trnasaction batch size"));
    add_param(ParamString(gdaldriver_, "gdaldriver", "GDAL driver (format), eg GPKG or PostgreSQL"));
    add_param(ParamString(layername_, "layername", "Layer name"));
    // add_param(ParamBool(overwrite_dataset_, "overwrite_dataset", "Overwrite dataset if it exists"));
    add_param(ParamBool(overwrite_layer_, "overwrite", "Overwrite layer. Otherwise data is appended."));
    add_param(ParamBool(require_attributes_, "require_attributes", "Only run when attributes input is connected"));
    add_param(ParamBool(create_directories_, "create_directories", "Create directories to write output file"));
    add_param(ParamStrMap(output_attribute_names, key_options, "output_attribute_names", "Output attribute names"));

    if (GDALGetDriverCount() == 0)
      GDALAllRegister();
  }
  bool parameters_valid() override {
    if (manager.substitute_globals(conn_string_).empty()) 
      return false;
    else 
      return true;
  }
  bool inputs_valid() override {
    if (require_attributes_) {
      return vector_input("geometries").has_data() && poly_input("attributes").has_data();
    } else {
      return vector_input("geometries").has_data();
    }
  }
  void process();

  void on_receive(gfMultiFeatureInputTerminal& it) override;
};

class GDALWriterNode : public Node {
  
  std::string filepath_ = "out.tif";
  std::string attribute_name = "identificatie";
  std::string gdaldriver_ = "GTiff";
  bool create_directories_ = true;

  public:
  using Node::Node;

  void init() {
    add_poly_input("image", {typeid(geoflow::Image)});
    add_poly_input("attributes", {typeid(bool), typeid(int), typeid(float), typeid(std::string), typeid(Date), typeid(Time), typeid(DateTime)});

    add_param(ParamString(attribute_name, "attribute_name", "attribute to use as filename. Has to be a string attribute."));
    add_param(ParamString(gdaldriver_, "gdaldriver", "driver to use"));
    add_param(ParamBool(create_directories_, "create_directories", "Create directories to write output file"));

    add_param(ParamPath(filepath_, "filepath", "File path"));

    if (GDALGetDriverCount() == 0)
      GDALAllRegister();
  }

  bool parameters_valid() override {
    if (manager.substitute_globals(filepath_).empty()) 
      return false;
    else 
      return true;
  }

  bool inputs_valid() override {
    return poly_input("image").has_data();
  }

  void process();
};

class GDALReaderNode : public Node {
  
  std::string filepath_;
  int bandnr_ = 1;

  public:
  using Node::Node;

  void init() {
    add_output("image", typeid(geoflow::Image));
    add_output("pointcloud", typeid(PointCollection));

    add_param(ParamPath(filepath_, "filepath", "File path"));
    add_param(ParamBoundedInt(bandnr_, 1, 1, "bandnr", "Band number to fetch"));

    if (GDALGetDriverCount() == 0)
      GDALAllRegister();
  }

  void process();
};

class CSVLoaderNode : public Node
{
  std::string filepath = "";
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
  std::string filepath = "out.csv";

public:
  using Node::Node;
  void init()
  {
    add_input("points", typeid(PointCollection));
    add_input("distances", typeid(vec1f));

    add_param(ParamPath(filepath, "filepath", "File path"));
  }
  void process();
  bool parameters_valid() override {
    if (manager.substitute_globals(filepath).empty()) 
      return false;
    else 
      return true;
  }
};

} // namespace geoflow::nodes::gdal
