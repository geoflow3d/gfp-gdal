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

void CSVPointLoaderNode::process()
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

void CSVSegmentLoaderNode::process()
{
  // SegmentCollection isegments;
  std::unordered_map<std::string, SegmentCollection> segments_by_bid;
  bool found_offset = manager.data_offset.has_value();
  for (auto filepath : split_string(manager.substitute_globals(filepaths), " ")) {
    std::ifstream f_in(filepath);
    float px, py, pz;
    size_t i = 0;
    std::vector<std::string> columns, attr_names;
    std::string line;
    std::getline(f_in, line);
    std::istringstream headerss;
    headerss.str(line);
    for (std::string column; std::getline(headerss, column, *separator.c_str()); ) {
      columns.push_back(column);
      if( !(
        column == "x_start" ||
        column == "y_start" ||
        column == "z_start" ||
        column == "x_end" ||
        column == "y_end" ||
        column == "z_end"
      )) {
        attr_names.push_back(column);
        // isegments.add_attribute_vec1s(column);
      }
    }
    size_t NC = columns.size();
    float xs, ys, zs, xe, ye, ze;
    while (std::getline(f_in, line)) {
      std::istringstream line_;
      line_.str(line);
      std::vector<std::string> vals;
      for (std::string ele; std::getline(line_, ele, *separator.c_str()); ) {
        vals.push_back(ele);
      }
      SegmentCollection* segments;
      for (size_t i=0; i<NC; ++i ) {
        if(columns[i]==aggregate_name) {
          auto result = segments_by_bid.emplace(std::make_pair(vals[i], SegmentCollection()));
          segments = &((*result.first).second);
          if(result.second) { // was just created; we need to initialise the attribute vecs
            for (auto& name : attr_names) {
              segments->add_attribute_vec1s(name);
            }
          } 
        }
      }
      for (size_t i=0; i<NC; ++i ) {
        if (columns[i] == "x_start") {
          xs = stof(vals[i]);
        } else if (columns[i] == "y_start") {
          ys = stof(vals[i]);
        } else if (columns[i] == "z_start") {
          zs = stof(vals[i]);
        } else if (columns[i] == "x_end") {
          xe = stof(vals[i]);
        } else if (columns[i] == "y_end") {
          ye = stof(vals[i]);
        } else if (columns[i] == "z_end") {
          ze = stof(vals[i]);
        } else {
          auto* attr = segments->get_attribute_vec1s(columns[i]);
          (*attr).push_back(vals[i]);
        }
      }
      if(!found_offset) {
        found_offset = true;
        (*manager.data_offset)[0] = xs;
        (*manager.data_offset)[1] = ys;
        (*manager.data_offset)[2] = zs;
      }
      segments->push_back({
        arr3f({xs - float((*manager.data_offset)[0]), ys - float((*manager.data_offset)[1]), zs - float((*manager.data_offset)[2])}),
        arr3f({xe - float((*manager.data_offset)[0]), ye - float((*manager.data_offset)[1]), ze - float((*manager.data_offset)[2])})
      });
      // eat up \n so that we reach eof after reading last line
      // f_in.get();
      // f_in.get();
      // f_in.get();
    }
    f_in.close();
  }


  // SegmentCollection isegments;
  for (auto& [bid,segs] : segments_by_bid) {
    output("segments").push_back(segs);
  }
}

void CSVWriterNode::process()
{
  auto& geom_term = input("geometry");
  size_t N = geom_term.size();

  auto file_path = manager.substitute_globals(filepath);
  auto parent_path = fs::path(file_path).parent_path();
  if (!parent_path.empty()) fs::create_directories(parent_path);
  std::ofstream f_out(file_path);
  f_out << std::fixed << std::setprecision(precision);

  if (geom_term.is_connected_type(typeid(PointCollection))) {
    f_out << "x" << separator;
    f_out << "y" << separator;
    f_out << "z" << separator;
  } else if (geom_term.is_connected_type(typeid(SegmentCollection))) {
    f_out << "x_start" << separator;
    f_out << "y_start" << separator;
    f_out << "z_start" << separator;
    f_out << "x_end" << separator;
    f_out << "y_end" << separator;
    f_out << "z_end" << separator;
  }

  // const attribute_vec_map* avm;
  if (geom_term.is_connected_type(typeid(PointCollection))) {
    auto& pc = geom_term.get<PointCollection>();
    auto& avm = pc.get_attributes();
    for (auto& [name, val]: avm) {
      f_out << name << separator;  
    }
  } else if (geom_term.is_connected_type(typeid(SegmentCollection))) {
    auto& sc = geom_term.get<SegmentCollection>();
    auto& avm = sc.get_attributes();
    for (auto& [name, val]: avm) {
      f_out << name << separator;  
    }
  }
  if (require_attributes_) {
    for (auto& term : poly_input("attributes").sub_terminals()) {
      auto search = output_attribute_names.find(term->get_full_name());
      if(search != output_attribute_names.end()) {
        if(!search->second.empty())
          f_out << search->second << separator;
      }
    }
  }
  f_out << "\n"; // end of header line
  if (geom_term.is_connected_type(typeid(PointCollection))) {
    for (size_t n=0; n<N; ++n){  
      auto& points = geom_term.get<PointCollection>(n);
      auto& avm = points.get_attributes();
      for (size_t i = 0; i < points.size(); ++i)
      {
        f_out
            << points[i][0] + (*manager.data_offset)[0] << separator
            << points[i][1] + (*manager.data_offset)[1] << separator
            << points[i][2] + (*manager.data_offset)[2] << separator;
        print_collection_attributes(f_out, avm, i);
        if (require_attributes_) print_attributes(f_out, n);
        f_out << "\n";
      }
    }
  } else if (geom_term.is_connected_type(typeid(SegmentCollection))) {
    for (size_t n=0; n<N; ++n){  
      auto& segments = geom_term.get<SegmentCollection>(n);
      auto& avm = segments.get_attributes();

      for (size_t i = 0; i < segments.size(); ++i)
      {
        f_out
            << segments[i][0][0] + (*manager.data_offset)[0] << separator
            << segments[i][0][1] + (*manager.data_offset)[1] << separator
            << segments[i][0][2] + (*manager.data_offset)[2] << separator
            << segments[i][1][0] + (*manager.data_offset)[0] << separator
            << segments[i][1][1] + (*manager.data_offset)[1] << separator
            << segments[i][1][2] + (*manager.data_offset)[2] << separator;
        print_collection_attributes(f_out, avm, i);
        if (require_attributes_) print_attributes(f_out, n);
        f_out << "\n";
      }
    }
  }

  f_out.close();
}

void CSVWriterNode::print_attributes(std::ofstream& f_out, const size_t& i) {
  for (auto& term : poly_input("attributes").sub_terminals()) {
    auto search = output_attribute_names.find(term->get_full_name());
    if(search != output_attribute_names.end()) {
      if(!search->second.empty()) {
        if (term->accepts_type(typeid(bool))) {
          f_out << term->get<bool>(i) << separator;
        } else if (term->accepts_type(typeid(float))) {
          f_out << term->get<float>(i) << separator;
        } else if (term->accepts_type(typeid(int))) {
          f_out << term->get<int>(i) << separator;
        } else if (term->accepts_type(typeid(std::string))) {
          f_out << term->get<std::string>(i) << separator;
        }
      }
    }
  }
}

void CSVWriterNode::print_collection_attributes(std::ofstream& f_out, const attribute_vec_map& avm, const size_t& i) {
  for (auto& [anme, attr] : avm) {
    if (auto vec = std::get_if<vec1b>(&attr)) {
      f_out << (*vec)[i] << separator;
    } else if (auto vec = std::get_if<vec1i>(&attr)) {
      f_out << (*vec)[i] << separator;
    } else if (auto vec = std::get_if<vec1s>(&attr)) {
      f_out << (*vec)[i] << separator;
    } else if (auto vec = std::get_if<vec1f>(&attr)) {
      f_out << (*vec)[i] << separator;
    }
  }
}

void CSVWriterNode::on_receive(gfMultiFeatureInputTerminal& it) {
  key_options.clear();
  if(&it == &poly_input("attributes")) {
    for(auto sub_term : it.sub_terminals()) {
      key_options.push_back(sub_term->get_full_name());
    }
  }
};

void GDALWriterNode::process() {

  auto& images = poly_input("image");

  const gfSingleFeatureOutputTerminal* id_term;
  auto id_attr_name = manager.substitute_globals(attribute_name);
  bool use_id_from_attribute = false;
  for (auto& term : poly_input("attributes").sub_terminals()) {
    if ( term->get_name() == id_attr_name && term->accepts_type(typeid(std::string)) ) {
      id_term = term;
      use_id_from_attribute = true;
    }
  }

  auto file_path = manager.substitute_globals(filepath_);
  if (use_id_from_attribute) {
    auto new_file_path = fs::path(file_path).parent_path() / id_term->get<const std::string>();
    new_file_path += fs::path(file_path).extension();
    file_path = new_file_path.string();
  }
  if(gdaldriver_ != "PostGISRaster" && create_directories_) fs::create_directories(fs::path(file_path).parent_path());
    
  GDALDriver *poDriver = GetGDALDriverManager()->GetDriverByName(gdaldriver_.c_str());
  GDALDataset *poDstDS;
  GDALDataType dataType;
  
  dataType = GDT_Float32;
  
  char **papszOptions = NULL;
  // TODO: should check if input images have the same dimension and cellsize....
  auto& image = images.sub_terminals()[0]->get<geoflow::Image>();
  poDstDS = poDriver->Create( file_path.c_str(), image.dim_x, image.dim_y, images.sub_terminals().size(), dataType,
                              papszOptions );
  double adfGeoTransform[6] = { image.min_x + (*manager.data_offset)[0], image.cellsize, 0, image.min_y + (*manager.data_offset)[1], 0, image.cellsize };
  
  auto no_data_val = image.nodataval;
  
  poDstDS->SetGeoTransform( adfGeoTransform );
  
  //    std::cout << oSRS.SetWellKnownGeogCS( WKGCS );
  //    std::cout << pszSRS_WKT <<std::endl;
  
  char *pszSRS_WKT = NULL;
//    oSRS.exportToWkt( &pszSRS_WKT );
//    poDstDS->SetProjection( pszSRS_WKT );
  CPLFree( pszSRS_WKT );
  
  size_t nBand = 1;
  GDALRasterBand *poBand;
  for (auto& sterm : images.sub_terminals()) {
    auto image = sterm->get<geoflow::Image>();

    // use same nodata value for all bands
    if (no_data_val != image.nodataval) {
      std::replace(image.array.begin(), image.array.end(), image.nodataval, no_data_val);
    }
  
    poBand = poDstDS->GetRasterBand(nBand++);
    auto error = poBand->RasterIO( GF_Write, 0, 0, image.dim_x, image.dim_y,
                      image.array.data(), image.dim_x, image.dim_y, dataType, 0, 0 );
    if (error == CE_Failure) {
      throw(gfException("Unable to write to raster"));
    }
    poBand->SetNoDataValue(no_data_val);
    poBand->SetDescription(sterm->get_name().c_str());
  }
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
  auto error = poBand->RasterIO( GF_Read, 0, 0, nXSize, nYSize,
                  pafImageData, nXSize, nYSize, GDT_Float32,
                  0, 0 );
  if (CE_Failure == error) {
    throw(gfException("Unable to open raster dataset"));
  }

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
