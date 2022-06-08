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

void OGRLoaderNode::push_attributes(OGRFeature &poFeature, std::unordered_map<std::string,int>& fieldNameMap)
{
  for (auto &[name, mterm] : poly_output("attributes").sub_terminals())
  {
    if (mterm->accepts_type(typeid(bool)))
    {
      mterm->push_back(bool(poFeature.GetFieldAsInteger(name.c_str())));
    }
    else if (mterm->accepts_type(typeid(int)))
    {
      mterm->push_back(int(poFeature.GetFieldAsInteger64(name.c_str())));
    }
    else if (mterm->accepts_type(typeid(float)))
    {
      mterm->push_back(float(poFeature.GetFieldAsDouble(name.c_str())));
    }
    else if (mterm->accepts_type(typeid(std::string)))
    {
      mterm->push_back((std::string)poFeature.GetFieldAsString(name.c_str()));
    }
    else if (mterm->accepts_type(typeid(Date)))
    {
      DateTime t;
      poFeature.GetFieldAsDateTime(fieldNameMap[name], &t.date.year, &t.date.month, &t.date.day, nullptr, nullptr, &t.time.second, nullptr);
      mterm->push_back(t.date);
    }
    else if (mterm->accepts_type(typeid(Time)))
    {
      Time time;
      poFeature.GetFieldAsDateTime(fieldNameMap[name], nullptr, nullptr, nullptr, &time.hour, &time.minute, &time.second, &time.timeZone);
      mterm->push_back(time);
    }
    else if (mterm->accepts_type(typeid(DateTime)))
    {
      DateTime t;
      poFeature.GetFieldAsDateTime(fieldNameMap[name], &t.date.year, &t.date.month, &t.date.day, &t.time.hour, &t.time.minute, &t.time.second, &t.time.timeZone);
      mterm->push_back(t);
    }
  }
}

void OGRLoaderNode::process()
{
  GDALDatasetUniquePtr poDS(GDALDataset::Open(manager.substitute_globals(filepath).c_str(), GDAL_OF_VECTOR));
  if (poDS == nullptr)
    throw(gfException("Open failed on " + manager.substitute_globals(filepath)));
  layer_count = poDS->GetLayerCount();
  std::cout << "Layer count: " << layer_count << "\n";
  if (layer_id >= layer_count) {
    throw(gfException("Illegal layer ID! Layer ID must be less than the layer count."));
  } else if (layer_id < 0) {
    throw(gfException("Illegal layer ID! Layer ID cannot be negative."));
  }

  // Set up vertex data (and buffer(s)) and attribute pointers
  // LineStringCollection line_strings;
  // LinearRingCollection linear_rings;
  auto &linear_rings = vector_output("linear_rings");
  auto &line_strings = vector_output("line_strings");
  
  auto &is_valid = vector_output("is_valid");
  auto &area = vector_output("area");

  OGRLayer *poLayer;
  poLayer = poDS->GetLayer(layer_id);
  if (poLayer == nullptr)
    throw(gfException("Could not get the selected layer (ID): " + std::to_string(layer_id)));
  std::cout << "Layer " << layer_id << " feature count: " << poLayer->GetFeatureCount() << "\n";
  geometry_type = poLayer->GetGeomType();
  geometry_type_name = OGRGeometryTypeToName(geometry_type);
  std::cout << "Layer geometry type: " << geometry_type_name << "\n";

  auto layer_def = poLayer->GetLayerDefn();
  auto field_count = layer_def->GetFieldCount();

  std::unordered_map<std::string,int> fieldNameMap;
  for (size_t i = 0; i < field_count; ++i)
  {
    auto field_def = layer_def->GetFieldDefn(i);
    auto t = field_def->GetType();
    auto field_name = (std::string)field_def->GetNameRef();
    fieldNameMap[field_name] = i;
    if ((t == OFTInteger) && (field_def->GetSubType() == OFSTBoolean)) 
    {
      auto &term = poly_output("attributes").add_vector(field_name, typeid(bool));
    } 
    else if (t == OFTInteger || t == OFTInteger64)
    {
      auto &term = poly_output("attributes").add_vector(field_name, typeid(int));
      // term.set(vec1i());
    }
    else if (t == OFTString)
    {
      auto &term = poly_output("attributes").add_vector(field_name, typeid(std::string));
      // term.set(vec1s());
    }
    else if (t == OFTReal)
    {
      auto &term = poly_output("attributes").add_vector(field_name, typeid(float));
      // term.set(vec1f());
    }
    else if (t == OFTDate)
    {
      auto &term = poly_output("attributes").add_vector(field_name, typeid(Date));
      // term.set(vec1f());
    }
    else if (t == OFTTime)
    {
      auto &term = poly_output("attributes").add_vector(field_name, typeid(Time));
      // term.set(vec1f());
    }
    else if (t == OFTDateTime)
    {
      auto &term = poly_output("attributes").add_vector(field_name, typeid(DateTime));
      // term.set(vec1f());
    }
  }

  bool found_offset = manager.data_offset.has_value();
  poLayer->ResetReading();

  for (auto &poFeature : poLayer)
  {
    // read feature geometry
    OGRGeometry *poGeometry;
    poGeometry = poFeature->GetGeometryRef();
    // std::cout << "Layer geometry type: " << poGeometry->getGeometryType() << " , " << geometry_type << "\n";
    if (poGeometry != nullptr) // FIXME: we should check if te layer geometrytype matches with this feature's geometry type. Messy because they can be a bit different eg. wkbLineStringZM and wkbLineString25D
    {

      if (
          poGeometry->getGeometryType() == wkbLineString25D || poGeometry->getGeometryType() == wkbLineStringZM ||
          poGeometry->getGeometryType() == wkbLineString)
      {
        OGRLineString *poLineString = poGeometry->toLineString();

        LineString line_string;
        for (auto &poPoint : poLineString)
        {
          if (!found_offset)
          {
            manager.data_offset = {poPoint.getX(), poPoint.getY(), 0};
            found_offset = true;
          }
          std::array<float, 3> p = {
              float(poPoint.getX() - (*manager.data_offset)[0]),
              float(poPoint.getY() - (*manager.data_offset)[1]),
              float(poPoint.getZ() - (*manager.data_offset)[2]) + base_elevation};
          line_string.push_back(p);
        }
        line_strings.push_back(line_string);
        is_valid.push_back(bool(poGeometry->IsValid()));

        push_attributes(*poFeature, fieldNameMap);
      }
      else if (poGeometry->getGeometryType() == wkbPolygon25D || poGeometry->getGeometryType() == wkbPolygon || poGeometry->getGeometryType() == wkbPolygonZM || poGeometry->getGeometryType() == wkbPolygonM)
      {
        OGRPolygon *poPolygon = poGeometry->toPolygon();

        LinearRing gf_polygon;
        // for(auto& poPoint : poPolygon->getExteriorRing()) {
        OGRPoint poPoint;
        auto ogr_ering = poPolygon->getExteriorRing();
        
        // ensure we output ccw exterior ring
        if ( ogr_ering->isClockwise() ) {
          ogr_ering->reverseWindingOrder();
        }
        for (size_t i = 0; i < ogr_ering->getNumPoints() - 1; ++i)
        {
          ogr_ering->getPoint(i, &poPoint);
          if (!found_offset)
          {
            manager.data_offset = {poPoint.getX(), poPoint.getY(), 0};
            found_offset = true;
          }
          std::array<float, 3> p = {float(poPoint.getX() - (*manager.data_offset)[0]), float(poPoint.getY() - (*manager.data_offset)[1]), float(poPoint.getZ() - (*manager.data_offset)[2]) + base_elevation};
          gf_polygon.push_back(p);
        }
        // also read the interior rings (holes)
        for (size_t i = 0; i < poPolygon->getNumInteriorRings(); ++i) 
        {
          auto ogr_iring = poPolygon->getInteriorRing(i);
          // ensure we output cw interior ring
          if ( !ogr_iring->isClockwise() ) {
            ogr_iring->reverseWindingOrder();
          }
          vec3f gf_iring;
          for (size_t j = 0; j < ogr_iring->getNumPoints() - 1; ++j)
          {
            ogr_iring->getPoint(j, &poPoint);
            std::array<float, 3> p = {float(poPoint.getX() - (*manager.data_offset)[0]), float(poPoint.getY() - (*manager.data_offset)[1]), float(poPoint.getZ() - (*manager.data_offset)[2]) + base_elevation};
            gf_iring.push_back(p);
          }
          gf_polygon.interior_rings().push_back(gf_iring);
        }
        // ring.erase(--ring.end());
        // bg::model::polygon<point_type_3d> boost_poly;
        // for (auto& p : ring) {
        //   bg::append(boost_poly.outer(), point_type_3d(p[0], p[1], p[2]));
        // }
        // bg::unique(boost_poly);
        // vec3f ring_dedup;
        // for (auto& p : boost_poly.outer()) {
        //   ring_dedup.push_back({float(bg::get<0>(p)), float(bg::get<1>(p)), float(bg::get<2>(p))}); //FIXME losing potential z...
        // }
        linear_rings.push_back(gf_polygon);
        
        area.push_back(float(poPolygon->get_Area()));
        is_valid.push_back(bool(poPolygon->IsValid()));

        push_attributes(*poFeature, fieldNameMap);
      }
      else
      {
        std::cout << "unsupported geometry\n";
      }
    }
  }
  // if (geometry_type == wkbLineString25D || geometry_type == wkbLineStringZM) {
  if (line_strings.size() > 0)
  {
    // output("line_strings").set(line_strings);
    std::cout << "pushed " << line_strings.size() << " line_string features...\n";
    // } else if (geometry_type == wkbPolygon || geometry_type == wkbPolygon25D || geometry_type == wkbPolygonZM || geometry_type == wkbPolygonM) {
  }
  else if (linear_rings.size() > 0)
  {
    // output("linear_rings").set(linear_rings);
    std::cout << "pushed " << linear_rings.size() << " linear_ring features...\n";
  }

  //    for(auto& [name, term] : poly_output("attributes").terminals) {
  //      std::cout << "group_term " << name << "\n";
  //      if (term->type == typeid(vec1f))
  //        for (auto& val : term->get<vec1f>()) {
  //          std::cout << "\t" << val << "\n";
  //        }
  //      if (term->type == typeid(vec1i))
  //        for (auto& val : term->get<vec1i>()) {
  //          std::cout << "\t" << val << "\n";
  //        }
  //      if (term->type == typeid(vec1s))
  //        for (auto& val : term->get<vec1s>()) {
  //          std::cout << "\t" << val << "\n";
  //        }
  //    }
}

} // namespace geoflow::nodes::gdal
