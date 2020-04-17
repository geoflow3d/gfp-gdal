#include "gdal_nodes.hpp"

#include <geos_c.h>

#include <unordered_map>
#include <variant>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace geoflow::nodes::gdal
{

void OGRLoaderNode::push_attributes(OGRFeature &poFeature)
{
  for (auto &[name, mterm] : poly_output("attributes").sub_terminals())
  {
    if (mterm->accepts_type(typeid(int)))
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
  }
}

void OGRLoaderNode::process()
{
  GDALDatasetUniquePtr poDS(GDALDataset::Open(manager.substitute_globals(filepath).c_str(), GDAL_OF_VECTOR));
  if (poDS == nullptr)
  {
    std::cerr << "Open failed.\n";
    return;
  }
  layer_count = poDS->GetLayerCount();
  std::cout << "Layer count: " << layer_count << "\n";
  layer_id = 0;

  // Set up vertex data (and buffer(s)) and attribute pointers
  // LineStringCollection line_strings;
  // LinearRingCollection linear_rings;
  auto &linear_rings = vector_output("linear_rings");
  auto &line_strings = vector_output("line_strings");

  OGRLayer *poLayer;
  poLayer = poDS->GetLayer(layer_id);
  std::cout << "Layer " << layer_id << " feature count: " << poLayer->GetFeatureCount() << "\n";
  geometry_type = poLayer->GetGeomType();
  geometry_type_name = OGRGeometryTypeToName(geometry_type);
  std::cout << "Layer geometry type: " << geometry_type_name << "\n";

  auto layer_def = poLayer->GetLayerDefn();
  auto field_count = layer_def->GetFieldCount();

  for (size_t i = 0; i < field_count; ++i)
  {
    auto field_def = layer_def->GetFieldDefn(i);
    auto t = field_def->GetType();
    auto field_name = (std::string)field_def->GetNameRef();
    if (t == OFTInteger || t == OFTInteger64)
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

        push_attributes(*poFeature);
      }
      else if (poGeometry->getGeometryType() == wkbPolygon25D || poGeometry->getGeometryType() == wkbPolygon || poGeometry->getGeometryType() == wkbPolygonZM || poGeometry->getGeometryType() == wkbPolygonM)
      {
        OGRPolygon *poPolygon = poGeometry->toPolygon();

        LinearRing gf_polygon;
        // for(auto& poPoint : poPolygon->getExteriorRing()) {
        OGRPoint poPoint;
        for (size_t i = 0; i < poPolygon->getExteriorRing()->getNumPoints() - 1; ++i)
        {
          poPolygon->getExteriorRing()->getPoint(i, &poPoint);
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

        push_attributes(*poFeature);
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
    printf("Creating field failed.\n");
    exit(1);
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
  auto& geom_term = vector_input("geometries");
  GDALDriver* poDriver;
  poDriver = GetGDALDriverManager()->GetDriverByName(manager.substitute_globals(gdaldriver).c_str());
  if (poDriver == nullptr) {
    printf("%s driver not available.\n", gdaldriver.c_str());
    exit(1);
  }

  // For parsing GDAL KEY=VALUE options, see the CSL* functions in
  // https://gdal.org/api/cpl.html#cpl-string-h

  // Driver creation options. For now there is only one option possible.
  //  char** papszOptions = (char**)CPLCalloc(sizeof(char*), 2);
  char** papszOptions = nullptr;
  if (append) {
    papszOptions = CSLSetNameValue(papszOptions, "APPEND_SUBDATASET", "YES");
    // If we append, we must overwrite too
    overwrite_dataset = true;
  }
  else {
    papszOptions = CSLSetNameValue(papszOptions, "APPEND_SUBDATASET", "NO");
    // We can still overwrite the layer though
  }

  GDALDataset* poDS;
  poDS = (GDALDataset*) GDALDataset::Open(manager.substitute_globals(filepath).c_str(), GDAL_OF_VECTOR||GDAL_OF_UPDATE);
  if (poDS == nullptr) {
    // Create the dataset
    poDS = poDriver->Create(manager.substitute_globals(filepath).c_str(),
                            0,
                            0,
                            0,
                            GDT_Unknown,
                            papszOptions);
  }

  if (poDS == nullptr) {
    printf("Creation of output file failed.\n");
    exit(1);
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
  if (overwrite_dataset)
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
      append             = false;
      overwrite_dataset  = false;
      bool tables_in_dsn = manager.substitute_globals(filepath).find(
                             "tables=") != std::string::npos;
      if (tables_in_dsn) {
        printf("You are creating a new table in PostgreSQL, but also specified "
               "the 'tables=' option in the connection string. GDAL will throw "
               "and error, the table name will be %s in the public schema, "
               "unless you also passed the schemas= option.\n",
               manager.substitute_globals(layername).c_str());
      }
    }
  } else {
    append            = false;
    overwrite_dataset = false;
  }

  std::unordered_map<std::string, size_t> attr_id_map;
  if (not append) {
    // overwrite or create, so field count needs to reset
    fcnt = 0;
    poLayer = poDS->CreateLayer(manager.substitute_globals(layername).c_str(), &oSRS, wkbType, lco);
    if (poLayer == nullptr) {
      printf("Layer creation failed for %s.\n",
             manager.substitute_globals(layername).c_str());
      exit(1);
    }
    // Create GDAL feature attributes
    for (auto& term : poly_input("attributes").sub_terminals()) {
      std::string name = term->get_name();
      //see if we need to map this name to another one
      auto search = output_attribute_names.find(name);
      if(search != output_attribute_names.end()) {
        if(search->second.size()!=0)
          name = search->second;
      }
      if (term->accepts_type(typeid(float))) {
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
  }
  else {
    // Fields already exist, so we need to map the poly_input("attributes")
    // names to the gdal layer names
    for (auto& term : poly_input("attributes").sub_terminals()) {
      std::string name = term->get_name();
      //NOTE BD: I'm not sure why is this needed
      auto search = output_attribute_names.find(name);
      if(search != output_attribute_names.end()) {
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

  // Add the attributes to the feature
  for (size_t i = 0; i != geom_term.size(); ++i) {
    OGRFeature* poFeature;
    poFeature = OGRFeature::CreateFeature(poLayer->GetLayerDefn());
    for (auto& term : poly_input("attributes").sub_terminals()) {
      auto tname = term->get_name();
      if (term->accepts_type(typeid(float))) {
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
      printf("Failed to create feature in %s.\n",
             manager.substitute_globals(gdaldriver).c_str());
      exit(1);
    }
    OGRFeature::DestroyFeature(poFeature);
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

void GEOSMergeLinesNode::process()
{
  std::cout << "Merging lines\n";
  auto lines = input("lines").get<LineStringCollection>();
  GEOSContextHandle_t gc = GEOS_init_r();
  std::vector<GEOSGeometry *> linearray;
  for (int i = 0; i < lines.size(); i++)
  {
    GEOSCoordSequence *points = GEOSCoordSeq_create_r(gc, 2, 3);
    for (int j = 0; j < 2; j++)
    {
      GEOSCoordSeq_setX_r(gc, points, j, lines[i][j][0]);
      GEOSCoordSeq_setY_r(gc, points, j, lines[i][j][1]);
      GEOSCoordSeq_setZ_r(gc, points, j, lines[i][j][2]);
    }
    GEOSGeometry *line = GEOSGeom_createLineString_r(gc, points);
    linearray.push_back(line);
  }
  GEOSGeometry *lineCollection = GEOSGeom_createCollection_r(gc, GEOS_GEOMETRYCOLLECTION, linearray.data(), lines.size());
  GEOSGeometry *mergedlines = GEOSLineMerge_r(gc, lineCollection);

  LineStringCollection outputLines;
  for (int i = 0; i < GEOSGetNumGeometries_r(gc, mergedlines); i++)
  {
    const GEOSCoordSequence *l = GEOSGeom_getCoordSeq_r(gc, GEOSGetGeometryN_r(gc, mergedlines, i));
    vec3f line_vec3f;
    unsigned int size;
    GEOSCoordSeq_getSize_r(gc, l, &size);
    for (int j = 0; j < size; j++)
    {
      double x, y, z = 0;
      GEOSCoordSeq_getX_r(gc, l, j, &x);
      GEOSCoordSeq_getY_r(gc, l, j, &y);
      GEOSCoordSeq_getZ_r(gc, l, j, &z);
      line_vec3f.push_back({float(x), float(y), float(z)});
    }
    outputLines.push_back(line_vec3f);
  }

  // clean GEOS geometries
  for (auto l : linearray)
  {
    GEOSGeom_destroy_r(gc, l);
  }
  //GEOSGeom_destroy_r(gc, lineCollection);
  GEOSGeom_destroy_r(gc, mergedlines);
  GEOS_finish_r(gc);

  output("lines").set(outputLines);
}

void PolygonUnionNode::process()
{
}

} // namespace geoflow::nodes::gdal
