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

#include <ogrsf_frmts.h>

namespace geoflow::nodes::gdal
{

class OGRLoaderNode : public Node
{
  int layer_count = 0;
  int layer_id = 0;
  std::string layer_name_ = "";
  std::string attribute_filter_ = "";
  float base_elevation = 0;
  bool output_fid_ = false;

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
    add_param(ParamBool(output_fid_, "output_fid", "Output attribute named 'OGR_FID' containing the OGR feature ID's"));
    add_param(ParamFloat(base_elevation, "base_elevation", "Base elevation"));
    add_param(ParamString(layer_name_, "layer_name", "Layer name (takes precedence over layer ID)"));
    add_param(ParamInt(layer_id, "layer_id", "Layer ID"));
    add_param(ParamString(attribute_filter_, "attribute_filter", "Load only features that satisfy this condition"));

    if (GDALGetDriverCount() == 0)
      GDALAllRegister();
  }
  void process();
};

class OGRWriterNode : public Node
{
  std::string srs = "EPSG:7415";
  std::string conn_string_ = "out";
  std::string gdaldriver_ = "GPKG";
  std::string layername_ = "geom";
  // bool overwrite_dataset_ = false;
  bool overwrite_layer_ = false;
  bool overwrite_file_ = false;
  bool create_directories_ = true;
  bool require_attributes_ = true;
  bool only_output_mapped_attrs_ = false;
  bool do_transactions_ = false;
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
    add_param(ParamText(srs, "CRS", "Coordinate reference system text. Can be EPSG code, WKT definition, etc."));
    add_param(ParamInt(transaction_batch_size_, "transaction_batch_size_", "Trnasaction batch size"));
    add_param(ParamString(gdaldriver_, "gdaldriver", "GDAL driver (format), eg GPKG or PostgreSQL"));
    add_param(ParamString(layername_, "layername", "Layer name"));
    // add_param(ParamBool(overwrite_dataset_, "overwrite_dataset", "Overwrite dataset if it exists"));
    add_param(ParamBool(overwrite_layer_, "overwrite_layer", "Overwrite layer. Otherwise data is appended."));
    add_param(ParamBool(overwrite_file_, "overwrite_file", "Overwrite entire file regardless of any layers."));
    add_param(ParamBool(require_attributes_, "require_attributes", "Only run when attributes input is connected"));
    add_param(ParamBool(create_directories_, "create_directories", "Create directories to write output file"));
    add_param(ParamBool(only_output_mapped_attrs_, "only_output_mapped_attrs", "Only output those attributes selected under Output attribute names"));
    add_param(ParamBool(do_transactions_, "do_transactions", "Attempt to use OGR transactions (for large number of feature writing)"));
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

class CSVPointLoaderNode : public Node
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

class CSVSegmentLoaderNode : public Node
{
  std::string filepaths = "";
  std::string separator = " ";
  std::string aggregate_name = "BuildingID";

public:
  using Node::Node;
  void init()
  {
    add_output("segments", typeid(SegmentCollection));

    add_param(ParamPath(filepaths, "filepaths", "File paths"));
  }
  void process();
};

class CSVWriterNode : public Node
{
  std::string filepath = "out.csv";
  std::string separator = " ";
  bool require_attributes_{true};
  int precision = 3;

  vec1s key_options;
  StrMap output_attribute_names;

public:
  using Node::Node;
  void init()
  {
    add_input("geometry", {typeid(PointCollection), typeid(SegmentCollection)});
    add_poly_input("attributes", {typeid(bool), typeid(int), typeid(float), typeid(std::string), typeid(Date), typeid(Time), typeid(DateTime)});

    add_param(ParamPath(filepath, "filepath", "File path"));
    add_param(ParamBool(require_attributes_, "require_attributes", "Only run when attributes input is connected"));
    add_param(ParamInt(precision, "precision", "Number of decimals for floating points"));
    add_param(ParamStrMap(output_attribute_names, key_options, "output_attribute_names", "Output attribute names"));
  }
  void process();

  void print_collection_attributes(std::ofstream& f_out, const attribute_vec_map& avm, const size_t& i);
  void print_attributes(std::ofstream& f_out, const size_t& i);

  bool parameters_valid() override {
    if (manager.substitute_globals(filepath).empty()) 
      return false;
    else 
      return true;
  }

  bool inputs_valid() override {
    if (require_attributes_) {
      return vector_input("geometry").has_data() && poly_input("attributes").has_data();
    } else {
      return vector_input("geometry").has_data();
    }
  }

  void on_receive(gfMultiFeatureInputTerminal& it) override;

};

} // namespace geoflow::nodes::gdal
