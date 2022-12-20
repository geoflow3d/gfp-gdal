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
#include "gdal_nodes.hpp"

#include <unordered_map>
#include <variant>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <filesystem>

namespace fs = std::filesystem;

namespace geoflow::nodes::gdal
{

inline void create_field(OGRLayer* layer, const std::string& name, OGRFieldType field_type) {
  OGRFieldDefn oField(name.c_str(), field_type);
  if (layer->CreateField(&oField) != OGRERR_NONE) {
    throw(gfException("Creating field failed"));
  }
}

OGRPolygon OGRWriterNode::create_polygon(const LinearRing& lr) {
  OGRPolygon ogrpoly;
  OGRLinearRing ogrring;
  // set exterior ring
  for (auto& g : lr) {
    auto coord_t = manager.coord_transform_rev(g[0], g[1], g[2]);
    ogrring.addPoint(coord_t[0],
                     coord_t[1],
                     coord_t[2]);
  }
  ogrring.closeRings();
  ogrpoly.addRing(&ogrring);

  // set interior rings
  for (auto& iring : lr.interior_rings()) {
    OGRLinearRing ogr_iring;
    for (auto& g : iring) {
      auto coord_t = manager.coord_transform_rev(g[0], g[1], g[2]);
      ogr_iring.addPoint(coord_t[0],
                         coord_t[1],
                         coord_t[2]);
    }
    ogr_iring.closeRings();
    ogrpoly.addRing(&ogr_iring);
  }
  return ogrpoly;
}

void OGRWriterNode::on_receive(gfMultiFeatureInputTerminal& it) {
  key_options.clear();
  if(&it == &poly_input("attributes")) {
    for(auto sub_term : it.sub_terminals()) {
      key_options.push_back(sub_term->get_full_name());
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

void OGRWriterNode::process()
{
  std::string connstr = manager.substitute_globals(conn_string_);
  std::string gdaldriver = manager.substitute_globals(gdaldriver_);
  std::string layername = manager.substitute_globals(layername_);

  connstr = substitute_from_term(connstr, poly_input("attributes"));

  auto& geom_term = vector_input("geometries");
  GDALDriver* driver;
  driver = GetGDALDriverManager()->GetDriverByName(gdaldriver.c_str());
  if (driver == nullptr) {
    throw(gfException(gdaldriver + " driver not available"));
  }

  if(gdaldriver != "PostgreSQL"){
    auto fpath = fs::path(connstr);
    if(overwrite_file_) {
      if(fs::exists(fpath))
        fs::remove(fpath);
    }
    if (create_directories_) {
      if(!fs::create_directories(fpath.parent_path()))
        std::cout << "Unable to create directories " << connstr << std::endl;
    }
  }

  GDALDataset* dataSource = nullptr;
  dataSource = (GDALDataset*) GDALOpenEx(connstr.c_str(), GDAL_OF_VECTOR|GDAL_OF_UPDATE, NULL, NULL, NULL);
  if (dataSource == nullptr) {
    dataSource = driver->Create(connstr.c_str(), 0, 0, 0, GDT_Unknown, NULL);
  }
  if (dataSource == nullptr) {
    throw(gfException("Starting database connection failed."));
  }
  if (do_transactions_) if (dataSource->StartTransaction() != OGRERR_NONE) {
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
  } else if (geom_term.is_connected_type(typeid(std::unordered_map<int, Mesh>))) {
    wkbType = wkbMultiPolygon25D;
  }
  
  std::unordered_map<std::string, size_t> attr_id_map;
  size_t fcnt(0);
  
  OGRLayer* layer = nullptr;
  char** lco = nullptr;
  if (overwrite_layer_) {
    lco = CSLSetNameValue(lco, "OVERWRITE", "YES");
  } else {
    lco = CSLSetNameValue(lco, "OVERWRITE", "NO");
    layer = dataSource->GetLayerByName(find_and_replace(layername, "-", "_").c_str());
  }

  auto geom_size = geom_term.size();
  std::cout << "creating " << geom_size << " geometry features\n";

  auto CRS = manager.substitute_globals(srs.c_str());
  if (layer == nullptr) {
    OGRSpatialReference oSRS;
    oSRS.SetFromUserInput(CRS.c_str());
    // oSRS.SetAxisMappingStrategy(OAMS_AUTHORITY_COMPLIANT);
    layer = dataSource->CreateLayer(layername.c_str(), &oSRS, wkbType, lco);

    // We set normalise_for_visualisation to true, becuase it seems that GDAL expects as the first coordinate easting/longitude when constructing geometries
    manager.set_rev_crs_transform(CRS.c_str(), true);

    // Create GDAL feature attributes
    for (auto& term : poly_input("attributes").sub_terminals()) {
      std::string name = term->get_full_name();
      std::cout << "Field " << name << " has a size of " << term->get_data_vec().size() << std::endl;
      if (geom_size != term->get_data_vec().size()) {
        throw(gfException("Number of attributes not equal to number of geometries [field name =" + name + "]"));
      }
      //see if we need to rename this attribute
      auto search = output_attribute_names.find(name);
      if(search != output_attribute_names.end()) {
        if(search->second.size()!=0) //ignore if the new name is an empty string
          name = search->second;
      } else if(only_output_mapped_attrs_) {
        continue; // skip attribute creation if not added by user in output_attribute_names
      }
      if (term->accepts_type(typeid(bool))) {
        OGRFieldDefn oField(name.c_str(), OFTInteger);
        oField.SetSubType(OFSTBoolean);
        if (layer->CreateField(&oField) != OGRERR_NONE) {
          throw(gfException("Creating field failed"));
        }
        attr_id_map[term->get_full_name()] = fcnt++;
      } else if (term->accepts_type(typeid(float))) {
        create_field(layer, name, OFTReal);
        attr_id_map[term->get_full_name()] = fcnt++;
      } else if (term->accepts_type(typeid(int))) {
        create_field(layer, name, OFTInteger64);
        attr_id_map[term->get_full_name()] = fcnt++;
      } else if (term->accepts_type(typeid(std::string))) {
        create_field(layer, name, OFTString);
        attr_id_map[term->get_full_name()] = fcnt++;
      } else if (term->accepts_type(typeid(Date))) {
        create_field(layer, name, OFTDate);
        attr_id_map[term->get_full_name()] = fcnt++;
      } else if (term->accepts_type(typeid(Time))) {
        create_field(layer, name, OFTTime);
        attr_id_map[term->get_full_name()] = fcnt++;
      } else if (term->accepts_type(typeid(DateTime))) {
        create_field(layer, name, OFTDateTime);
        attr_id_map[term->get_full_name()] = fcnt++;
      }
    }
    if (geom_term.is_connected_type(typeid(MultiTriangleCollection)) || geom_term.is_connected_type(typeid(std::unordered_map<int, Mesh>))) {
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

      // TODO: Don't hardcode building_part_id for these geometry types.
      //  Would be better get all attributes from the attribute terminal.
      const std::string building_part_id = "building_part_id";
      create_field(layer, building_part_id, OFTString);
      attr_id_map[building_part_id] = fcnt++;
    }
  } else {
    // Fields already exist, so we need to map the poly_input("attributes")
    // names to the gdal layer names
    // But: what if layer has a different set of attributes?
    fcnt = layer->GetLayerDefn()->GetFieldCount();
    for (auto& term : poly_input("attributes").sub_terminals()) {
      std::string name = term->get_full_name();
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
      } else if(only_output_mapped_attrs_) {
        continue; // skip attribute creation if not added by user in output_attribute_names
      }
      // attr_id_map[geoflow attribute name] = gdal field index
      for (int i=0; i < fcnt; i++) {
        auto fdef = layer->GetLayerDefn()->GetFieldDefn(i);
        if (strcmp(fdef->GetNameRef(), name.c_str()) == 0)
          attr_id_map[term->get_full_name()] = i;
      }
    }
    if (geom_term.is_connected_type(typeid(MultiTriangleCollection)) || geom_term.is_connected_type(typeid(std::unordered_map<int, Mesh>))) {
      for (int i = 0; i < fcnt; i++) {
        auto fdef = layer->GetLayerDefn()->GetFieldDefn(i);
        if (strcmp(fdef->GetNameRef(), "labels") == 0) {
          attr_id_map["labels"] = i;
        } else if (strcmp(fdef->GetNameRef(), "building_part_id") == 0) {
          attr_id_map["building_part_id"] = i;
        }
      }
    }
  }
  if (do_transactions_) if (dataSource->CommitTransaction() != OGRERR_NONE) {
    throw(gfException("Creating database transaction failed.\n"));
  }
  if (do_transactions_) if (dataSource->StartTransaction() != OGRERR_NONE) {
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
      auto tname = term->get_full_name();
      
      // skip if not added by user in output_attribute_names
      auto search = output_attribute_names.find(tname);
      if(only_output_mapped_attrs_ && search == output_attribute_names.end()) {
        continue;
      }

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
      } else if (term->accepts_type(typeid(Date))) {
        auto& val = term->get<const Date&>(i);
        poFeature->SetField(attr_id_map[tname], val.year, val.month, val.day);
      } else if (term->accepts_type(typeid(Time))) {
        auto& val = term->get<const Time&>(i);
        poFeature->SetField(attr_id_map[tname], 0, 0, 0, val.hour, val.minute, val.second, val.timeZone);
      } else if (term->accepts_type(typeid(Time))) {
        auto& val = term->get<const DateTime&>(i);
        poFeature->SetField(attr_id_map[tname], val.date.year, val.date.month, val.date.day, val.time.hour, val.time.minute, val.time.second, val.time.timeZone);
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
          auto coord_t = manager.coord_transform_rev(g[0], g[1], g[2]);
          ogrlinestring.addPoint(coord_t[0],
                                 coord_t[1],
                                 coord_t[2]);
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
              auto coord_t = manager.coord_transform_rev(vertex[0], vertex[1], vertex[2]);
              ring.addPoint(coord_t[0],
                            coord_t[1],
                            coord_t[2]);
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
        auto&           mtcs = geom_term.get<MultiTriangleCollection>(i);

        for (size_t j=0; j<mtcs.tri_size(); j++) {
          const auto& tc = mtcs.tri_at(j);
          auto poFeature_ = poFeature->Clone();

          // create an empty multipolygon for this TriangleCollection
          OGRMultiPolygon ogrmultipoly = OGRMultiPolygon();
          for (auto& triangle : tc) {
            OGRPolygon    ogrpoly = OGRPolygon();
            OGRLinearRing ring    = OGRLinearRing();
            for (auto& vertex : triangle) {
              auto coord_t = manager.coord_transform_rev(vertex[0], vertex[1], vertex[2]);
              ring.addPoint(coord_t[0],
                            coord_t[1],
                            coord_t[2]);
            }
            ring.closeRings();
            ogrpoly.addRing(&ring);
            if (ogrmultipoly.addGeometry(&ogrpoly) != OGRERR_NONE) {
              printf("couldn't add triangle to MultiPolygonZ");
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

            auto bp_id = std::to_string(mtcs.building_part_ids_[j]);
            poFeature_->SetField(attr_id_map["building_part_id"], bp_id.c_str());
          }
          poFeatures.push_back(poFeature_);
        }
        OGRFeature::DestroyFeature(poFeature);
      } else if (geom_term.is_connected_type(typeid(Mesh))) {
        auto&           mesh         = geom_term.get<Mesh>(i);
        OGRMultiPolygon ogrmultipoly = OGRMultiPolygon();
        for (auto& poly : mesh.get_polygons()) {
          auto ogrpoly = create_polygon(poly);
          if (ogrmultipoly.addGeometry(&ogrpoly) != OGRERR_NONE) {
            printf("couldn't add polygon to MultiPolygon");
          }
        }
        poFeature->SetGeometry(&ogrmultipoly);
        poFeatures.push_back(poFeature);
      } else if (geom_term.is_connected_type(typeid(std::unordered_map<int, Mesh>))) {
        const auto& meshes = geom_term.get<std::unordered_map<int, Mesh>>(i);

        for ( const auto& [mid, mesh] : geom_term.get<std::unordered_map<int, Mesh>>(i) ) {
          auto poFeature_ = poFeature->Clone();

          OGRMultiPolygon ogrmultipoly = OGRMultiPolygon();
          for (auto& poly : mesh.get_polygons()) {
            auto ogrpoly = create_polygon(poly);
            if (ogrmultipoly.addGeometry(&ogrpoly) != OGRERR_NONE) {
              printf("couldn't add polygon to MultiPolygonZ");
            }
          }

          size_t label_size = mesh.get_labels().size();
          std::vector<int> val(label_size);
          val = mesh.get_labels();
          poFeature_->SetField(attr_id_map["labels"], label_size, val.data());

          auto bp_id = std::to_string(mid);
          poFeature_->SetField(attr_id_map["building_part_id"], bp_id.c_str());

          poFeature_->SetGeometry(&ogrmultipoly);
          poFeatures.push_back(poFeature_);
        }
        OGRFeature::DestroyFeature(poFeature);
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
      if (do_transactions_) if (dataSource->CommitTransaction() != OGRERR_NONE) {
        throw(gfException("Committing features to database failed.\n"));
      }
      if (do_transactions_) if (dataSource->StartTransaction() != OGRERR_NONE) {
        throw(gfException("Starting database transaction failed.\n"));
      }
    }
  }

  if (do_transactions_) if (dataSource->CommitTransaction() != OGRERR_NONE) {
    throw(gfException("Committing features to database failed.\n"));
  }

  GDALClose(dataSource);
//  GDALClose(driver);
}

} // namespace geoflow::nodes::gdal
