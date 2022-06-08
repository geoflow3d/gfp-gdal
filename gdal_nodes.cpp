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
