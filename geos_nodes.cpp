#include <geos_c.h>

#include "geos_nodes.hpp"


namespace geoflow::nodes::gfp_geos {

    GEOSContextHandle_t gc;

    void print_geos_message(const char * message, ...) {
        std::cerr << message << std::endl;
    }

    template<typename T> void to_geos_linear_ring(const T& lr, GEOSGeometry *& g_lr) {
        const auto size = lr.size();
        GEOSCoordSequence *g_coord_seq = GEOSCoordSeq_create_r(gc, size+1, 3);
        for (size_t i=0; i<size; ++i) {
            GEOSCoordSeq_setX_r(gc, g_coord_seq, i, lr[i][0]);
            GEOSCoordSeq_setY_r(gc, g_coord_seq, i, lr[i][1]);
            GEOSCoordSeq_setZ_r(gc, g_coord_seq, i, lr[i][2]);
        }
        // close the ring
        GEOSCoordSeq_setX_r(gc, g_coord_seq, size, lr[0][0]);
        GEOSCoordSeq_setY_r(gc, g_coord_seq, size, lr[0][1]);
        GEOSCoordSeq_setZ_r(gc, g_coord_seq, size, lr[0][2]);

        g_lr = GEOSGeom_createLinearRing_r(gc, g_coord_seq);
    }

    void to_geos_polygon(const LinearRing& lr, GEOSGeometry *& g_polygon) {

        GEOSGeometry *g_exterior = nullptr;
        to_geos_linear_ring(lr, g_exterior);

        std::vector<GEOSGeometry *> g_holes;
        for (auto& hole : lr.interior_rings()) {
            GEOSGeometry *g_hole = nullptr;
            to_geos_linear_ring(hole, g_hole);
            g_holes.push_back(g_hole);
        }
        if(g_holes.size()==0)
            g_polygon = GEOSGeom_createPolygon_r(gc, g_exterior, nullptr, 0);
        else
            g_polygon = GEOSGeom_createPolygon_r(gc, g_exterior, g_holes.data(), g_holes.size());
    }

    template<typename T> void from_geos_linear_ring(const GEOSGeometry* g_lin_ring, T& gf_ring) {
        const GEOSCoordSequence* g_coord_seq = GEOSGeom_getCoordSeq_r(gc, g_lin_ring);
        unsigned int size, dims;
        GEOSCoordSeq_getSize_r(gc, g_coord_seq, &size);
        GEOSCoordSeq_getDimensions_r(gc, g_coord_seq, &dims);

        // note we do not repeat the first coordinate
        for (size_t i = 0; i < (size-1); ++i) {
            double x, y, z = 0;
            GEOSCoordSeq_getX_r(gc, g_coord_seq, i, &x);
            GEOSCoordSeq_getY_r(gc, g_coord_seq, i, &y);
            if (dims==3) GEOSCoordSeq_getZ_r(gc, g_coord_seq, i, &z);
            gf_ring.push_back({float(x),float(y),float(z)});
        }
    }

    LinearRing from_geos_polygon(const GEOSGeometry* g_polygon) {
        LinearRing lr;
        const GEOSGeometry* g_ring = GEOSGetExteriorRing_r(gc, g_polygon);
        from_geos_linear_ring(g_ring, lr);
        for (size_t i=0; i<GEOSGetNumInteriorRings_r(gc, g_polygon); ++i) {
            auto g_iring = GEOSGetInteriorRingN_r(gc, g_polygon, i);
            vec3f hole;
            from_geos_linear_ring(g_ring, hole);
            lr.interior_rings().push_back(hole);
        }
        return lr;
    }

    void PolygonSimplifyGEOSNode::process() {
        auto& ipolygons = vector_input("polygons");
        auto& opolygons = vector_output("simplified_polygons");
        
        gc = GEOS_init_r();
        GEOSContext_setNoticeHandler_r(gc, print_geos_message);
        GEOSContext_setErrorHandler_r(gc, print_geos_message);

        for (size_t i=0; i<ipolygons.size(); ++i) {
            auto& lr = ipolygons.get<LinearRing>(i);
            
            GEOSGeometry* g_polygon = nullptr;
            to_geos_polygon(lr, g_polygon);

            if(GEOSisValid_r(gc, g_polygon)!=1) {
                std::cout << "feature not simplified\n";
            } 

            GEOSGeometry* simplified_geom = GEOSSimplify_r(gc, g_polygon, double(tolerance));

            if(GEOSisValid_r(gc, simplified_geom)!=1) {
                std::cout << "feature not simplified\n";
                opolygons.push_back(lr);
            } else {
                opolygons.push_back( from_geos_polygon(simplified_geom) );
            }

            GEOSGeom_destroy_r(gc, g_polygon);
            GEOSGeom_destroy_r(gc, simplified_geom);
        }
        GEOS_finish_r(gc);
    }

    void PolygonBufferGEOSNode::process() {
        auto& ipolygons = vector_input("polygons");
        auto& opolygons = vector_output("offset_polygons");
        
        gc = GEOS_init_r();

        for (size_t i=0; i<ipolygons.size(); ++i) {
            auto& lr = ipolygons.get<LinearRing>(i);
            
            GEOSGeometry* g_polygon = nullptr;
            to_geos_polygon(lr, g_polygon);

            GEOSGeometry* buffered_geom = GEOSBuffer_r(gc, g_polygon, double(offset), 8);

            if(GEOSisValid_r(gc, buffered_geom)!=1) {
                std::cout << "feature not simplified\n";
                opolygons.push_back(lr);
            } else {
                opolygons.push_back( from_geos_polygon(buffered_geom) );
            }

            GEOSGeom_destroy_r(gc, g_polygon);
            GEOSGeom_destroy_r(gc, buffered_geom);
        }
        GEOS_finish_r(gc);
    }


    void GEOSMergeLinesNode::process()
    {
        std::cout << "Merging lines\n";
        auto lines = input("lines").get<LineStringCollection>();
        gc = GEOS_init_r();
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

} //namespace geoflow::nodes::stepedge