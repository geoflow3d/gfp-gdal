#include "gdal_nodes.hpp"

#include <unordered_map>
#include <variant>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace geoflow::nodes::gdal
{

inline void create_field(OGRLayer* layer, const std::string& name, OGRFieldType field_type) {
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
  } else if (geom_term.is_connected_type(typeid(std::vector<TriangleCollection>)) || geom_term.is_connected_type(typeid(MultiTriangleCollection))) {
    // Note that in case of a MultiTriangleCollection we actually write the
    // TriangleCollections separately, and not the whole MultiTriangleCollection
    // to a single feature. That's why a MultiPolygon and not an aggregate of
    // multipolygons.
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

  auto geom_size = geom_term.size();
  std::cout << "creating " << geom_size << " geometry features\n";

  if (layer == nullptr) {
    OGRSpatialReference oSRS;
    oSRS.importFromEPSG(epsg);
    layer = dataSource->CreateLayer(layername.c_str(), &oSRS, wkbType, lco);

    // Create GDAL feature attributes
    for (auto& term : poly_input("attributes").sub_terminals()) {
      std::string name = term->get_name();
      std::cout << "Field " << name << " has a size of " << term->get_data_vec().size() << std::endl;
      if (geom_size != term->get_data_vec().size()) {
        throw(gfException("Number of attributes not equal to number of geometries [field name =" + name + "]"));
      }
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
    if (geom_term.is_connected_type(typeid(MultiTriangleCollection))) {
      // TODO: Ideally we would handle the attributes of all geometry types the same way and wouldn't need to do cases like this one.
      // A MultiTriangleCollection stores the attributes with itself
      // if (geom_term.has_data()) 
      //   if (!geom_term.get_data_vec()[0].has_value()) std::cout << 
      //     if (geom_term.get<MultiTriangleCollection>(0).has_attributes()) {

      //   auto& mtc = geom_term.get<MultiTriangleCollection>(0);
      //   // Get the AttributeMap of the first TriangleCollection. We expect
      //   // here that each TriangleCollection has the same attributes.
      //   AttributeMap attr_map = mtc.get_attributes()[0];
      //   for (const auto& a : attr_map) {
      //     // TODO: since we have a variant here, in case the first value is empty I don't know what to do
      //     std::string k = a.first;
      //     if (!a.second.empty()) {
      //       attribute_value v = a.second[0];
      //       if (std::holds_alternative<int>(v))
      //         create_field(layer, (std::string&)k, OFTInteger64List);
      //       else if (std::holds_alternative<float>(v))
      //         create_field(layer, (std::string&)k, OFTRealList);
      //       else if (std::holds_alternative<std::string>(v))
      //         create_field(layer, (std::string&)k, OFTStringList);
      //       else if (std::holds_alternative<bool>(v)) {
      //         // There is no BooleanList, so they are written as integers
      //         create_field(layer, (std::string&)k, OFTIntegerList);
      //       }
      //     }
      //     attr_id_map[k] = fcnt++;
      //   }
      // }
      const std::string labels = "labels";
      create_field(layer, labels, OFTIntegerList);
      attr_id_map[labels] = fcnt++;
    }
  } else {
    // Fields already exist, so we need to map the poly_input("attributes")
    // names to the gdal layer names
    // But: what if layer has a different set of attributes?
    fcnt = layer->GetLayerDefn()->GetFieldCount();
    for (auto& term : poly_input("attributes").sub_terminals()) {
      std::string name = term->get_name();
      std::cout << "Field " << name << " has a size of " << term->get_data_vec().size() << std::endl;
      if (geom_size != term->get_data_vec().size()) {
        throw(gfException("Number of attributes not equal to number of geometries [field name =" + name + "]"));
      }
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
    if (geom_term.is_connected_type(typeid(MultiTriangleCollection))) {
      for (int i=0; i < fcnt; i++) {
        auto fdef = layer->GetLayerDefn()->GetFieldDefn(i);
//        auto& mtc = geom_term.get<MultiTriangleCollection>(0);
//        AttributeMap attr_map = mtc.get_attributes()[0];
//        for (const auto& a : attr_map) {
//          if (strcmp(fdef->GetNameRef(), a.first.c_str()) == 0)
//            attr_id_map[a.first] = i;
//        }
        if (strcmp(fdef->GetNameRef(), "labels") == 0)
          attr_id_map["labels"] = i;
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
    // create a vec of features for the case where we write multiple feature rows (building with multiple parts)
    std::vector<OGRFeature*> poFeatures;
    // Add the attributes to the feature
    for (auto& term : poly_input("attributes").sub_terminals()) {
      if (!term->get_data_vec()[i].has_value()) continue;
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
    if (!geom_term.get_data_vec()[i].has_value()) {
      // set to an empty geometry
      poFeature->SetGeometry(OGRGeometryFactory::createGeometry(wkbType));
    } else {
      if (geom_term.is_connected_type(typeid(LinearRing))) {
        const LinearRing &lr = geom_term.get<LinearRing>(i);
        OGRPolygon ogrpoly = create_polygon(lr);
        poFeature->SetGeometry(&ogrpoly);
        poFeatures.push_back(poFeature);
      } else if (geom_term.is_connected_type(typeid(LineString))) {
        OGRLineString ogrlinestring;
        const LineString &ls = geom_term.get<LineString>(i);
        for (auto &g : ls) {
          ogrlinestring.addPoint(g[0] + (*manager.data_offset)[0],
                                 g[1] + (*manager.data_offset)[1],
                                 g[2] + (*manager.data_offset)[2]);
        }
        poFeature->SetGeometry(&ogrlinestring);
        poFeatures.push_back(poFeature);
      } else if (geom_term.is_connected_type(typeid(std::vector<TriangleCollection>))) {
        OGRMultiPolygon ogrmultipoly = OGRMultiPolygon();
        auto& tcs = geom_term.get<std::vector<TriangleCollection>>(i);

        for (auto& tc : tcs) {  
          auto poFeature_ = poFeature->Clone();
          for (auto &triangle : tc) {
            OGRPolygon ogrpoly = OGRPolygon();
            OGRLinearRing ring = OGRLinearRing();
            for (auto &vertex : triangle) {
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
          poFeature_->SetGeometry(&ogrmultipoly);
          poFeatures.push_back(poFeature_);
        }
        OGRFeature::DestroyFeature(poFeature);
      } else if (geom_term.is_connected_type(typeid(MultiTriangleCollection))) {
        OGRMultiPolygon ogrmultipoly = OGRMultiPolygon();
        auto&           mtcs = geom_term.get<MultiTriangleCollection>(i);

        for (size_t j=0; j<mtcs.tri_size(); j++) {
          const auto& tc = mtcs.tri_at(j);
          auto poFeature_ = poFeature->Clone();
          for (auto& triangle : tc) {
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
          poFeature_->SetGeometry(&ogrmultipoly);
          if (mtcs.has_attributes()) {
            for (const auto& attr_map : mtcs.attr_at(j)) {
              if (attr_map.second.empty()) poFeature_->SetFieldNull(attr_id_map[attr_map.first]);
              else {
                // Since the 'attribute_value' type is a 'variant' and therefore
                // the 'attr_map' AttributeMap is a vector of variants, the
                // SetField method does not recognize the data type stored
                // within the variant. So it doesn't write the values unless we
                // put the values into an array with an explicit type. I tried
                // passing attr_map.second.data() to SetField but doesn't work.
                attribute_value v = attr_map.second[0];
                if (std::holds_alternative<int>(v)) {
                  std::vector<int> val(attr_map.second.size());
                  for (size_t h=0; h<attr_map.second.size(); h++) {
                    val[h] = std::get<int>(attr_map.second[h]);
                  }
                  poFeature_->SetField(attr_id_map[attr_map.first], attr_map.second.size(), val.data());
                }
                else if (std::holds_alternative<float>(v)) {
                  std::vector<double> val(attr_map.second.size());
                  for (size_t h=0; h<attr_map.second.size(); h++) {
                    val[h] = (double) std::get<float>(attr_map.second[h]);
                  }
                  poFeature_->SetField(attr_id_map[attr_map.first], attr_map.second.size(), val.data());
                }
                else if (std::holds_alternative<std::string>(v)) {
                  // FIXME: needs to align the character encoding with the encoding of the database, otherwise will throw an 'ERROR:  invalid byte sequence for encoding ...'
//                  const char* val[attr_map.second.size()];
//                  for (size_t h=0; h<attr_map.second.size(); h++) {
//                    val[h] = std::get<std::string>(attr_map.second[h]).c_str();
//                  }
//                  poFeature_->SetField(attr_id_map[attr_map.first], attr_map.second.size(), val);
                }
                else if (std::holds_alternative<bool>(v)) {
                  std::vector<int> val(attr_map.second.size());
                  for (size_t h=0; h<attr_map.second.size(); h++) {
                    val[h] = std::get<bool>(attr_map.second[h]);
                  }
                  poFeature_->SetField(attr_id_map[attr_map.first], attr_map.second.size(), val.data());
                }
                else throw(gfException("Unsupported attribute value type for: " + attr_map.first));
              }
            }
          }
          poFeatures.push_back(poFeature_);
        }
        OGRFeature::DestroyFeature(poFeature);
      } else if (geom_term.is_connected_type(typeid(Mesh))) {
        auto &mesh = geom_term.get<Mesh>(i);
        OGRMultiPolygon ogrmultipoly = OGRMultiPolygon();
        for (auto &poly : mesh.get_polygons()) {
          auto ogrpoly = create_polygon(poly);
          if (ogrmultipoly.addGeometry(&ogrpoly) != OGRERR_NONE) {
            printf("couldn't add polygon to MultiPolygon");
          }
        }
        poFeature->SetGeometry(&ogrmultipoly);
        poFeatures.push_back(poFeature);
      } else {
        std::cerr << "Unsupported type of input geometry " << geom_term.get_connected_type().name() << std::endl;
      }
    }

    for (auto poFeat : poFeatures) {
      if (layer->CreateFeature(poFeat) != OGRERR_NONE) {
        throw(gfException("Failed to create feature in "+gdaldriver));
      }
      OGRFeature::DestroyFeature(poFeat);
    }

    if (i % transaction_batch_size_ == 0) {
      if (dataSource->CommitTransaction() != OGRERR_NONE) {
        throw(gfException("Committing features to database failed.\n"));
      }
      if (dataSource->StartTransaction() != OGRERR_NONE) {
        throw(gfException("Starting database transaction failed.\n"));
      }
    }
  }

  if (dataSource->CommitTransaction() != OGRERR_NONE) {
    throw(gfException("Committing features to database failed.\n"));
  }

  GDALClose(dataSource);
//  GDALClose(driver);
}

} // namespace geoflow::nodes::gdal
