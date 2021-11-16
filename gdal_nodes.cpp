#include "gdal_nodes.hpp"

#include <geos_c.h>
#include <gdal_priv.h>

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

inline void create_field(OGRLayer* poLayer, std::string& name, OGRFieldType field_type) {
  OGRFieldDefn oField(name.c_str(), field_type);
  if (poLayer->CreateField(&oField) != OGRERR_NONE) {
    throw(gfException("Creating field failed"));
  }
}

OGRPolygon OGRWriterNode::create_polygon(const LinearRing& lr) {
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

void OGRWriterNode::on_receive(gfMultiFeatureInputTerminal& it) {
  key_options.clear();
  if(&it == &poly_input("attributes")) {
    for(auto sub_term : it.sub_terminals()) {
      key_options.push_back(sub_term->get_name());
    }
  }
};

void OGRWriterNode::process()
{
  bool overwrite = overwrite_;
  bool append = overwrite ? false : append_;
  std::string filepath = manager.substitute_globals(filepath_);
  std::string gdaldriver = manager.substitute_globals(gdaldriver_);
  std::string layername = manager.substitute_globals(layername_);

  auto& geom_term = vector_input("geometries");
  GDALDriver* poDriver;
  poDriver = GetGDALDriverManager()->GetDriverByName(gdaldriver.c_str());
  if (poDriver == nullptr) {
    throw(gfException(gdaldriver + " driver not available"));
  }

  // For parsing GDAL KEY=VALUE options, see the CSL* functions in
  // https://gdal.org/api/cpl.html#cpl-string-h

  // Driver creation options. For now there is only one option possible.
  //  char** papszOptions = (char**)CPLCalloc(sizeof(char*), 2);
  char** papszOptions = nullptr;
  if (append) {
    papszOptions = CSLSetNameValue(papszOptions, "APPEND_SUBDATASET", "YES");
    // If we append, we must overwrite too
    overwrite = true;
  }
  else {
    papszOptions = CSLSetNameValue(papszOptions, "APPEND_SUBDATASET", "NO");
    // We can still overwrite the layer though
  }

  GDALDataset* poDS;
  poDS = (GDALDataset*) GDALDataset::Open(filepath.c_str(), GDAL_OF_VECTOR||GDAL_OF_UPDATE);
  if (poDS == nullptr) {
    // Create the dataset
    poDS = poDriver->Create(filepath.c_str(),
                            0,
                            0,
                            0,
                            GDT_Unknown,
                            papszOptions);
  }
  std::cout << "\nUsing driver: " << poDS->GetDriverName() << "\n";
  bool do_transactions = poDS->TestCapability(ODsCTransactions);
  std::cout << "Transactions support: " << (do_transactions ? "yes" : "no") << "\n";

  if (poDS == nullptr) {
    throw(gfException("Creation/Opening of output file failed."));
  }

  if (do_transactions && poDS->StartTransaction() != OGRERR_NONE) {
    throw(gfException("Starting database transaction failed.\n"));
  }

  OGRSpatialReference oSRS;
  OGRLayer*           poLayer;

  oSRS.importFromEPSG(epsg);
  OGRwkbGeometryType wkbType;
  if (geom_term.is_connected_type(typeid(LinearRing))) {
    wkbType = wkbPolygon;
  } else if (geom_term.is_connected_type(typeid(LineString))) {
    wkbType = wkbLineString25D;
  } else if (geom_term.is_connected_type(typeid(TriangleCollection))) {
    wkbType = wkbMultiPolygon25D;
  }

  char** lco = nullptr;
  if (overwrite)
    lco = CSLSetNameValue(lco, "OVERWRITE", "YES");
  else
    lco = CSLSetNameValue(lco, "OVERWRITE", "NO");

  int fcnt(0);
  int layer_cnt = poDS->GetLayerCount();
  if (layer_cnt > 0) {
    poLayer = poDS->GetLayer(0);
    // When the driver is PostgreSQL AND the tables= option is used in the
    // connection string, the GetLayer() always gets a layer, even
    // if the table doesn't exist. So we need to check for existing fields.
    fcnt = poLayer->GetLayerDefn()->GetFieldCount();
    if (fcnt == 0) {
      append = false;
      overwrite  = false;
      bool tables_in_dsn = filepath.find(
                             "tables=") != std::string::npos;
      if (tables_in_dsn) {
        printf("You are creating a new table in PostgreSQL, but also specified "
               "the 'tables=' option in the connection string. GDAL will throw "
               "and error, the table name will be %s in the public schema, "
               "unless you also passed the schemas= option.\n",
               layername.c_str());
      }
    }
  } else {
    append = false;
    overwrite = false;
  }

  std::unordered_map<std::string, size_t> attr_id_map;
  if (!append) {
    // overwrite or create, so field count needs to reset
    fcnt = 0;
    poLayer = poDS->CreateLayer(layername.c_str(), &oSRS, wkbType, lco);
    if (poLayer == nullptr) {             
      throw(gfException("Layer creation failed for " + layername));
    }
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
        if (poLayer->CreateField(&oField) != OGRERR_NONE) {
          throw(gfException("Creating field failed"));
        }
        attr_id_map[term->get_name()] = fcnt++;
      } else if (term->accepts_type(typeid(float))) {
        create_field(poLayer, name, OFTReal);
        attr_id_map[term->get_name()] = fcnt++;
      } else if (term->accepts_type(typeid(int))) {
        create_field(poLayer, name, OFTInteger64);
        attr_id_map[term->get_name()] = fcnt++;
      } else if (term->accepts_type(typeid(std::string))) {
        create_field(poLayer, name, OFTString);
        attr_id_map[term->get_name()] = fcnt++;
      }
    }
    if (do_transactions && poDS->CommitTransaction() != OGRERR_NONE) {
      throw(gfException("Creating database transaction failed.\n"));
    }
    if (do_transactions && poDS->StartTransaction() != OGRERR_NONE) {
      throw(gfException("Starting database transaction failed.\n"));
    }
  } else {
    // Fields already exist, so we need to map the poly_input("attributes")
    // names to the gdal layer names
    // But: what if layer has a different set of attributes?
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
        auto fdef = poLayer->GetLayerDefn()->GetFieldDefn(i);
        if (strcmp(fdef->GetNameRef(), name.c_str()) == 0)
          attr_id_map[term->get_name()] = i;
      }
    }
  }

  for (size_t i = 0; i != geom_term.size(); ++i) {
    OGRFeature* poFeature;
    poFeature = OGRFeature::CreateFeature(poLayer->GetLayerDefn());
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

    if (poLayer->CreateFeature(poFeature) != OGRERR_NONE) {
      throw(gfException("Failed to create feature in "+gdaldriver));
    }
    OGRFeature::DestroyFeature(poFeature);
  }
  if (do_transactions && poDS->CommitTransaction() != OGRERR_NONE) {
    throw(gfException("Committing features to database failed.\n"));
  }

  GDALClose(poDS);
  CSLDestroy(papszOptions);
}

void CSVLoaderNode::process()
{
  PointCollection points;

  std::ifstream f_in(manager.substitute_globals(filepath));
  float px, py, pz;
  size_t i = 0;
  std::string header;
  std::getline(f_in, header);
  while (f_in >> px >> py >> pz)
  {
    if (i++ % thin_nth == 0)
    {
      points.push_back({px, py, pz});
    }
  }
  f_in.close();

  output("points").set(points);
}

void CSVWriterNode::process()
{
  auto points = input("points").get<PointCollection>();
  auto distances = input("distances").get<vec1f>();

  std::ofstream f_out(manager.substitute_globals(filepath));
  f_out << std::fixed << std::setprecision(2);
  f_out << "x y z distance\n";
  for (size_t i = 0; i < points.size(); ++i)
  {
    f_out
        << points[i][0] + (*manager.data_offset)[0] << " "
        << points[i][1] + (*manager.data_offset)[1] << " "
        << points[i][2] + (*manager.data_offset)[2] << " "
        << distances[i] << "\n";
  }
  f_out.close();
}

void GDALWriterNode::process() {

  auto image = input("image").get<geoflow::Image>();

  auto file_path = manager.substitute_globals(filepath_);
  fs::create_directories(fs::path(file_path).parent_path());
    
  GDALDriver *poDriver = GetGDALDriverManager()->GetDriverByName("GTiff");
  GDALDataset *poDstDS;
  GDALDataType dataType;
  
  dataType = GDT_Float32;
  
  char **papszOptions = NULL;
  poDstDS = poDriver->Create( file_path.c_str(), image.dim_x, image.dim_y, 1, dataType,
                              papszOptions );
  double adfGeoTransform[6] = { image.min_x + (*manager.data_offset)[0], image.cellsize, 0, image.min_y + (*manager.data_offset)[1], 0, image.cellsize };
  GDALRasterBand *poBand;
  
  poDstDS->SetGeoTransform( adfGeoTransform );
  
  //    std::cout << oSRS.SetWellKnownGeogCS( WKGCS );
  //    std::cout << pszSRS_WKT <<std::endl;
  
  char *pszSRS_WKT = NULL;
//    oSRS.exportToWkt( &pszSRS_WKT );
//    poDstDS->SetProjection( pszSRS_WKT );
  CPLFree( pszSRS_WKT );
  
  poBand = poDstDS->GetRasterBand(1);
  poBand->RasterIO( GF_Write, 0, 0, image.dim_x, image.dim_y,
                    image.array.data(), image.dim_x, image.dim_y, dataType, 0, 0 );
  poBand->SetNoDataValue(image.nodataval);
  /* Once we're done, close properly the dataset */
  GDALClose( (GDALDatasetH) poDstDS );
}

void GDALReaderNode::process() {
  // open file
  GDALDataset  *poDataset;
  GDALAllRegister();
  poDataset = (GDALDataset *) GDALOpen( filepath_.c_str(), GA_ReadOnly );
  if( poDataset == NULL )
  {
    return;
  }

  // get metadata
  double adfGeoTransform[6];
  printf( "Driver: %s/%s\n",
          poDataset->GetDriver()->GetDescription(),
          poDataset->GetDriver()->GetMetadataItem( GDAL_DMD_LONGNAME ) );
  printf( "Size is %dx%dx%d\n",
          poDataset->GetRasterXSize(), poDataset->GetRasterYSize(),
          poDataset->GetRasterCount() );
  if( poDataset->GetProjectionRef()  != NULL )
      printf( "Projection is `%s'\n", poDataset->GetProjectionRef() );
  if( poDataset->GetGeoTransform( adfGeoTransform ) == CE_None )
  {
      printf( "Origin = (%.6f,%.6f)\n",
              adfGeoTransform[0], adfGeoTransform[3] );
      printf( "Pixel Size = (%.6f,%.6f)\n",
              adfGeoTransform[1], adfGeoTransform[5] );
  }

  // fetch band
  GDALRasterBand  *poBand;
  int             nBlockXSize, nBlockYSize;
  int             bGotMin, bGotMax;
  double          adfMinMax[2];
  // use GetRasterCount() to get nr of bands available
  poBand = poDataset->GetRasterBand( bandnr_ );
  poBand->GetBlockSize( &nBlockXSize, &nBlockYSize );
  printf( "Block=%dx%d Type=%s, ColorInterp=%s\n",
          nBlockXSize, nBlockYSize,
          GDALGetDataTypeName(poBand->GetRasterDataType()),
          GDALGetColorInterpretationName(
              poBand->GetColorInterpretation()) );
  adfMinMax[0] = poBand->GetMinimum( &bGotMin );
  adfMinMax[1] = poBand->GetMaximum( &bGotMax );
  if( ! (bGotMin && bGotMax) )
      GDALComputeRasterMinMax((GDALRasterBandH)poBand, TRUE, adfMinMax);
  printf( "Min=%.3fd, Max=%.3f\n", adfMinMax[0], adfMinMax[1] );
  if( poBand->GetOverviewCount() > 0 )
      printf( "Band has %d overviews.\n", poBand->GetOverviewCount() );
  if( poBand->GetColorTable() != NULL )
      printf( "Band has a color table with %d entries.\n",
              poBand->GetColorTable()->GetColorEntryCount() );

  // read raster data from band
  float *pafImageData;
  int   nXSize = poBand->GetXSize();
  int   nYSize = poBand->GetYSize();
  pafImageData = (float *) CPLMalloc(sizeof(float)*nXSize*nYSize);
  poBand->RasterIO( GF_Read, 0, 0, nXSize, nYSize,
                  pafImageData, nXSize, nYSize, GDT_Float32,
                  0, 0 );

  PointCollection pointcloud;
  for (size_t i=0; i<nXSize; ++i) {
    for (size_t j=0; j<nYSize; ++j) {
      pointcloud.push_back( {
        float(adfGeoTransform[0] + adfGeoTransform[1] * i - (*manager.data_offset)[0]),
        float(adfGeoTransform[3] + adfGeoTransform[5] * j - (*manager.data_offset)[1]),
        pafImageData[i + j*nXSize] - float((*manager.data_offset)[2])
      } );
    }
  }

  // free memory
  CPLFree(pafImageData);

  output("pointcloud").set(pointcloud);

}

} // namespace geoflow::nodes::gdal
