// #include <geos_c.h>

#include <geos/geom/GeometryFactory.h>
#include <geos/geom/Coordinate.h>
#include <geos/geom/Polygon.h>
#include <geos/geom/CoordinateArraySequence.h>
#include <geos/algorithm/Orientation.h>

#include <geos/simplify/DouglasPeuckerSimplifier.h>

#include "geos_nodes.hpp"


namespace geoflow::nodes::gfp_geos {

    geos::geom::GeometryFactory::Ptr geos_global_factory;

    std::unique_ptr<geos::geom::Polygon> to_geos_polygon(const LinearRing& lr) {
        auto coordSeq = std::make_unique<geos::geom::CoordinateArraySequence>();
        for (auto&p : lr) {
            coordSeq->add(geos::geom::Coordinate(p[0], p[1]));
        }
        coordSeq->add(geos::geom::Coordinate(lr[0][0], lr[0][1]));
        if (!geos::algorithm::Orientation::isCCW(coordSeq.get())){
            geos::geom::CoordinateArraySequence::reverse(coordSeq.get());
        }
        auto linearRing = geos_global_factory->createLinearRing(std::move(coordSeq));
        return geos_global_factory->createPolygon(std::move(linearRing));
    }

    void PolygonSimplifyGEOSNode::process() {
        auto& ipolygons = vector_input("polygons");
        auto& opolygons = vector_output("simplified_polygons");

        geos_global_factory = geos::geom::GeometryFactory::create();

        for (size_t i=0; i<ipolygons.size(); ++i) {
            auto& lr = ipolygons.get<LinearRing>(i);
            
            auto polygon = to_geos_polygon(lr);

            auto simplified_geom = geos::simplify::DouglasPeuckerSimplifier::simplify(polygon.get(), tolerance).release();
            // auto buf_geom = polygon->buffer(offset).release();

            if(!simplified_geom->isValid()) {
                std::cout << "feature not simplified\n";
                opolygons.push_back(lr);
            } else if (auto buf_poly = dynamic_cast<geos::geom::Polygon*>(simplified_geom)) {
                auto polygon_ring = buf_poly->getExteriorRing();

                LinearRing lr_offset;
                for (size_t i=0; i<polygon_ring->getNumPoints()-1; ++i) {
                    auto& p = polygon_ring->getCoordinateN(i);
                    lr_offset.push_back(arr3f{float(p.x), float(p.y), 0});
                }
                opolygons.push_back(lr_offset);                
            } else {
                std::cout << "feature not simplified\n";
                opolygons.push_back(lr);
            }

        }
    }

    void PolygonBufferGEOSNode::process() {
        auto& ipolygons = vector_input("polygons");
        auto& opolygons = vector_output("offset_polygons");

        geos_global_factory = geos::geom::GeometryFactory::create();

        for (size_t i=0; i<ipolygons.size(); ++i) {
            auto& lr = ipolygons.get<LinearRing>(i);
            
            auto polygon = to_geos_polygon(lr);

            auto buf_geom = polygon->buffer(offset).release();
            
            if(!buf_geom->isValid()) {
                std::cout << "feature not buffered\n";
                opolygons.push_back(lr);
            } else if (auto buf_poly = dynamic_cast<geos::geom::Polygon*>(buf_geom)) {
                auto polygon_ring = buf_poly->getExteriorRing();

                LinearRing lr_offset;
                for (size_t i=0; i<polygon_ring->getNumPoints()-1; ++i) {
                    auto& p = polygon_ring->getCoordinateN(i);
                    lr_offset.push_back(arr3f{float(p.x), float(p.y), 0});
                }
                // }
                opolygons.push_back(lr_offset);
            } else {
                std::cout << "feature not buffered\n";
                opolygons.push_back(lr);
            }
            delete buf_geom;
        }
    }


    // void GEOSMergeLinesNode::process()
    // {
    //     std::cout << "Merging lines\n";
    //     auto lines = input("lines").get<LineStringCollection>();
    //     GEOSContextHandle_t gc = GEOS_init_r();
    //     std::vector<GEOSGeometry *> linearray;
    //     for (int i = 0; i < lines.size(); i++)
    //     {
    //         GEOSCoordSequence *points = GEOSCoordSeq_create_r(gc, 2, 3);
    //         for (int j = 0; j < 2; j++)
    //         {
    //         GEOSCoordSeq_setX_r(gc, points, j, lines[i][j][0]);
    //         GEOSCoordSeq_setY_r(gc, points, j, lines[i][j][1]);
    //         GEOSCoordSeq_setZ_r(gc, points, j, lines[i][j][2]);
    //         }
    //         GEOSGeometry *line = GEOSGeom_createLineString_r(gc, points);
    //         linearray.push_back(line);
    //     }
    //     GEOSGeometry *lineCollection = GEOSGeom_createCollection_r(gc, GEOS_GEOMETRYCOLLECTION, linearray.data(), lines.size());
    //     GEOSGeometry *mergedlines = GEOSLineMerge_r(gc, lineCollection);

    //     LineStringCollection outputLines;
    //     for (int i = 0; i < GEOSGetNumGeometries_r(gc, mergedlines); i++)
    //     {
    //         const GEOSCoordSequence *l = GEOSGeom_getCoordSeq_r(gc, GEOSGetGeometryN_r(gc, mergedlines, i));
    //         vec3f line_vec3f;
    //         unsigned int size;
    //         GEOSCoordSeq_getSize_r(gc, l, &size);
    //         for (int j = 0; j < size; j++)
    //         {
    //         double x, y, z = 0;
    //         GEOSCoordSeq_getX_r(gc, l, j, &x);
    //         GEOSCoordSeq_getY_r(gc, l, j, &y);
    //         GEOSCoordSeq_getZ_r(gc, l, j, &z);
    //         line_vec3f.push_back({float(x), float(y), float(z)});
    //         }
    //         outputLines.push_back(line_vec3f);
    //     }

    //     // clean GEOS geometries
    //     for (auto l : linearray)
    //     {
    //         GEOSGeom_destroy_r(gc, l);
    //     }
    //     //GEOSGeom_destroy_r(gc, lineCollection);
    //     GEOSGeom_destroy_r(gc, mergedlines);
    //     GEOS_finish_r(gc);

    //     output("lines").set(outputLines);
    // }

} //namespace geoflow::nodes::stepedge