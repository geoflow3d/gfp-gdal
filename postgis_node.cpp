#include "gdal_nodes.hpp"

#include <unordered_map>
#include <variant>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace geoflow::nodes::gdal
{

inline void create_field(OGRLayer* layer, std::string& name, OGRFieldType field_type) {
  OGRFieldDefn oField(name.c_str(), field_type);
  if (layer->CreateField(&oField) != OGRERR_NONE) {
    throw(gfException("Creating field failed"));
  }
}

OGRPolygon OGRPostGISWriterNode::create_polygon(const LinearRing& lr) {
  OGRPolygon ogrpoly;
  OGRLinearRing ogrring;
  // set exterior ring
  for (auto& g : lr) {
    ogrring.addPoint(g[0] + (*manager.data_offset)[0],
                      g[1] + (*manager.data_offset)[1],
                      g[2] + (*manager.data_offset)[2]);
  }
  ogrring.closeRings();
  ogrpoly.addRing(&ogrring);

  // set interior rings
  for (auto& iring : lr.interior_rings()) {
    OGRLinearRing ogr_iring;
    for (auto& g : iring) {
      ogr_iring.addPoint(g[0] + (*manager.data_offset)[0],
                          g[1] + (*manager.data_offset)[1],
                          g[2] + (*manager.data_offset)[2]);
    }
    ogr_iring.closeRings();
    ogrpoly.addRing(&ogr_iring);
  }
  return ogrpoly;
}

void OGRPostGISWriterNode::on_receive(gfMultiFeatureInputTerminal& it) {
  key_options.clear();
  if(&it == &poly_input("attributes")) {
    for(auto sub_term : it.sub_terminals()) {
      key_options.push_back(sub_term->get_name());
    }
  }
};

/// Find and replace a substring with another substring
inline std::string find_and_replace(std::string str, std::string from, std::string to) {

  std::size_t start_pos = 0;

  while((start_pos = str.find(from, start_pos)) != std::string::npos) {
    str.replace(start_pos, from.length(), to);
    start_pos += to.length();
  }

  return str;
}

void OGRPostGISWriterNode::process()
{
  std::string connstr = manager.substitute_globals(conn_string_);
  std::string gdaldriver = manager.substitute_globals(gdaldriver_);
  std::string layername = manager.substitute_globals(layername_);

  auto& geom_term = vector_input("geometries");
  GDALDriver* driver;
  driver = GetGDALDriverManager()->GetDriverByName(gdaldriver.c_str());
  if (driver == nullptr) {
    throw(gfException(gdaldriver + " driver not available"));
  }

  GDALDataset* dataSource = driver->Create(connstr.c_str(), 0, 0, 0, GDT_Unknown, NULL);
  if (dataSource == nullptr) {
    throw(gfException("Starting database connection failed."));
  }
  if (dataSource->StartTransaction() != OGRERR_NONE) {
    throw(gfException("Starting database transaction failed.\n"));
  }

  OGRwkbGeometryType wkbType;
  if (geom_term.is_connected_type(typeid(LinearRing))) {
    wkbType = wkbPolygon;
  } else if (geom_term.is_connected_type(typeid(LineString))) {
    wkbType = wkbLineString25D;
  } else if (geom_term.is_connected_type(typeid(TriangleCollection))) {
    wkbType = wkbMultiPolygon25D;
  }
  
  std::unordered_map<std::string, size_t> attr_id_map;
  size_t fcnt(0);
  
  OGRLayer* layer = nullptr;
  char** lco = nullptr;
  if (overwrite_) {
    lco = CSLSetNameValue(lco, "OVERWRITE", "YES");
  } else {
    lco = CSLSetNameValue(lco, "OVERWRITE", "NO");
    layer = dataSource->GetLayerByName(find_and_replace(layername, "-", "_").c_str());
  }

  if (layer == nullptr) {
    OGRSpatialReference oSRS;
    oSRS.importFromEPSG(epsg);
    layer = dataSource->CreateLayer(layername.c_str(), &oSRS, wkbType, lco);

    // Create GDAL feature attributes
    for (auto& term : poly_input("attributes").sub_terminals()) {
      std::string name = term->get_name();
      //see if we need to rename this attribute
      auto search = output_attribute_names.find(name);
      if(search != output_attribute_names.end()) {
        //ignore if the new name is an empty string
        if(search->second.size()!=0)
          name = search->second;
      }
      if (term->accepts_type(typeid(bool))) {
        OGRFieldDefn oField(name.c_str(), OFTInteger);
        oField.SetSubType(OFSTBoolean);
        if (layer->CreateField(&oField) != OGRERR_NONE) {
          throw(gfException("Creating field failed"));
        }
        attr_id_map[term->get_name()] = fcnt++;
      } else if (term->accepts_type(typeid(float))) {
        create_field(layer, name, OFTReal);
        attr_id_map[term->get_name()] = fcnt++;
      } else if (term->accepts_type(typeid(int))) {
        create_field(layer, name, OFTInteger64);
        attr_id_map[term->get_name()] = fcnt++;
      } else if (term->accepts_type(typeid(std::string))) {
        create_field(layer, name, OFTString);
        attr_id_map[term->get_name()] = fcnt++;
      }
    }
  } else {
    // Fields already exist, so we need to map the poly_input("attributes")
    // names to the gdal layer names
    // But: what if layer has a different set of attributes?
    fcnt = layer->GetLayerDefn()->GetFieldCount();
    for (auto& term : poly_input("attributes").sub_terminals()) {
      std::string name = term->get_name();
      //see if we need to rename this attribute
      auto search = output_attribute_names.find(name);
      if(search != output_attribute_names.end()) {
        //ignore if the new name is an empty string
        if(search->second.size()!=0)
          name = search->second;
      }
      // attr_id_map[geoflow attribute name] = gdal field index
      for (int i=0; i < fcnt; i++) {
        auto fdef = layer->GetLayerDefn()->GetFieldDefn(i);
        if (strcmp(fdef->GetNameRef(), name.c_str()) == 0)
          attr_id_map[term->get_name()] = i;
      }
    }
  }
  if (dataSource->CommitTransaction() != OGRERR_NONE) {
    throw(gfException("Creating database transaction failed.\n"));
  }
  if (dataSource->StartTransaction() != OGRERR_NONE) {
    throw(gfException("Starting database transaction failed.\n"));
  }

  for (size_t i = 0; i != geom_term.size(); ++i) {
    OGRFeature* poFeature;
    poFeature = OGRFeature::CreateFeature(layer->GetLayerDefn());
    // Add the attributes to the feature
    for (auto& term : poly_input("attributes").sub_terminals()) {
      auto tname = term->get_name();
      if (term->accepts_type(typeid(bool))) {
        auto& val = term->get<const bool&>(i);
        poFeature->SetField(attr_id_map[tname], val);
      } else if (term->accepts_type(typeid(float))) {
        auto& val = term->get<const float&>(i);
        poFeature->SetField(attr_id_map[tname], val);
      } else if (term->accepts_type(typeid(int))) {
        auto& val = term->get<const int&>(i);
        poFeature->SetField(attr_id_map[tname], val);
      } else if (term->accepts_type(typeid(std::string))) {
        auto& val = term->get<const std::string&>(i);
        poFeature->SetField(attr_id_map[tname], val.c_str());
      }
    }

    // Geometry input type handling for the feature
    // Cast the incoming geometry to the appropriate GDAL type. Note that this
    // need to be in line with what is set for wkbType above.
    if (geom_term.is_connected_type(typeid(LinearRing))) {
      const LinearRing& lr = geom_term.get<LinearRing>(i);
      OGRPolygon ogrpoly = create_polygon(lr);
      poFeature->SetGeometry(&ogrpoly);
    }
    if (geom_term.is_connected_type(typeid(LineString))) {
      OGRLineString     ogrlinestring;
      const LineString& ls = geom_term.get<LineString>(i);
      for (auto& g : ls) {
        ogrlinestring.addPoint(g[0] + (*manager.data_offset)[0],
                               g[1] + (*manager.data_offset)[1],
                               g[2] + (*manager.data_offset)[2]);
      }
      poFeature->SetGeometry(&ogrlinestring);
    }
    // Note BD: only tried this with Postgis
    if (geom_term.is_connected_type(typeid(TriangleCollection))) {
      OGRMultiPolygon ogrmultipoly = OGRMultiPolygon();
      for (auto& triangle : geom_term.get<TriangleCollection>(i)) {
        OGRPolygon    ogrpoly = OGRPolygon();
        OGRLinearRing ring    = OGRLinearRing();
        for (auto& vertex : triangle) {
          ring.addPoint(vertex[0] + (*manager.data_offset)[0],
                        vertex[1] + (*manager.data_offset)[1],
                        vertex[2] + (*manager.data_offset)[2]);
        }
        ring.closeRings();
        ogrpoly.addRing(&ring);
        if (ogrmultipoly.addGeometry(&ogrpoly) != OGRERR_NONE) {
          printf("couldn't add triangle to MultiSurfaceZ");
        }
      }
      poFeature->SetGeometry(&ogrmultipoly);
    }

    if (geom_term.is_connected_type(typeid(Mesh))) {
      auto& mesh = geom_term.get<Mesh>(i);
      OGRMultiPolygon ogrmultipoly = OGRMultiPolygon();
      for (auto& poly : mesh.get_polygons()) {
        auto ogrpoly = create_polygon(poly);
        if (ogrmultipoly.addGeometry(&ogrpoly) != OGRERR_NONE) {
          printf("couldn't add polygon to MultiPolygon");
        }
      }
      poFeature->SetGeometry(&ogrmultipoly);
    }

    if (layer->CreateFeature(poFeature) != OGRERR_NONE) {
      throw(gfException("Failed to create feature in "+gdaldriver));
    }
    OGRFeature::DestroyFeature(poFeature);
    if (i % transaction_batch_size_ == 0) {
      if (dataSource->CommitTransaction() != OGRERR_NONE) {
        throw(gfException("Committing features to database failed.\n"));
      }
      if (dataSource->StartTransaction() != OGRERR_NONE) {
        throw(gfException("Starting database transaction failed.\n"));
      }
    }
  }

  GDALClose(dataSource);
//  GDALClose(driver);
}

} // namespace geoflow::nodes::gdal