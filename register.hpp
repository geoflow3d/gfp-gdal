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
#include "geos_nodes.hpp"

using namespace geoflow::nodes::gdal;
using namespace geoflow::nodes::gfp_geos;

void register_nodes(geoflow::NodeRegister &node_register)
{
  node_register.register_node<OGRLoaderNode>("OGRLoader");
  node_register.register_node<OGRWriterNode>("OGRWriter");

  node_register.register_node<GDALWriterNode>("GDALWriter");
  node_register.register_node<GDALReaderNode>("GDALReader");

  node_register.register_node<CSVLoaderNode>("CSVLoader");
  node_register.register_node<CSVWriterNode>("CSVWriter");
  
  node_register.register_node<GEOSMergeLinesNode>("GEOSMergeLines");
  node_register.register_node<PolygonBufferGEOSNode>("PolygonBufferGEOS");
  node_register.register_node<PolygonSimplifyGEOSNode>("PolygonSimplifyGEOS");
  // node_register.register_node<PolygonOrientNode>("PolygonOrient");
}