/*
 * Copyright (c) 2012-2015, Association Scientifique pour la Geologie et ses Applications (ASGA)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the <organization> nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  Contacts:
 *     Arnaud.Botella@univ-lorraine.fr
 *     Antoine.Mazuyer@univ-lorraine.fr
 *     Jeanne.Pellerin@wias-berlin.de
 *
 *     http://www.ring-team.org
 *
 *     RING Project
 *     Ecole Nationale Superieure de Geologie - GeoRessources
 *     2 Rue du Doyen Marcel Roubault - TSA 70605
 *     54518 VANDOEUVRE-LES-NANCY
 *     FRANCE
 */

#include <ringmesh/io.h>

#include <ctime>

#include <geogram/basic/file_system.h>
#include <geogram/basic/line_stream.h>
#include <geogram/basic/logger.h>
#include <geogram/basic/algorithm.h>

#include <ringmesh/ringmesh_config.h>
#include <ringmesh/geo_model.h>
#include <ringmesh/geo_model_api.h>
#include <ringmesh/geo_model_builder.h>
#include <ringmesh/geo_model_validity.h>

namespace {
    using RINGMesh::index_t ;
    using RINGMesh::GeoModel ;
    using RINGMesh::Region ;
    using RINGMesh::GeoModelElement ;
    using RINGMesh::GME ;
    using RINGMesh::Surface ;
    using RINGMesh::Line ;
    using RINGMesh::Corner ;
    using RINGMesh::vec3 ;


    /*!
    * From a given file name (MyFile.ext), create a MyFile directory 
    * in the directory containing that file, or the current working directory.
    * @param[in] filename the filename 
    * @param[out] path full path to the created directory 
    * @param[out] directory name of the created directory
    */
    void create_directory_from_filename(
        const std::string& filename,
        std::string& path,
        std::string& directory)
    {
        path = GEO::FileSystem::dir_name( filename ) ;
        directory = GEO::FileSystem::base_name( filename, true ) ;
        if( path == "." ) {
            path = GEO::FileSystem::get_current_working_directory() ;
        }
        path += "/" + directory ;
        GEO::FileSystem::create_directory( path ) ;
    }

    /*!
    * @brief Total number of facets in the Surfaces of a BM
    */
    inline index_t nb_facets( const GeoModel& BM )
    {
        index_t result = 0 ;
        for( index_t i = 0; i < BM.nb_surfaces(); ++i ) {
            result += BM.surface( i ).nb_cells() ;
        }
        return result ;
    }

    /*!
    * @brief Write a region information in a stream
    * @details Used by function to save the Model in a .ml file
    *
    * @param[in] count Region index in the file
    * @param[in] region The region to save
    * @param[in,out] out The file output stream
    */
    void save_region( index_t count, const Region& region, std::ostream& out )
    {
        out << "REGION " << count << "  " << region.name() << " " << std::endl ;
        index_t it = 0 ;

        for( index_t i = 0; i < region.nb_boundaries(); ++i ) {
            out << "  " ;
            if( region.side( i ) ) {
                out << "+" ;
            } else {
                out << "-" ;
            }
            out << region.boundary( i ).index()+1 ;
            it++ ;
            if( it == 5 ) {
                out << std::endl ;
                it = 0 ;
            }
        }
        out << "  0" << std::endl ;
    }

    /*!
    * @brief Write information for on layer in a stream
    * @details Used by function to save the Model in a .ml file
    *
    * @param[in] count Index of the layer in the file
    * @param[in] offset Offset of region indices in the file
    * @param[in] layer The layer to write
    * @param[in,out] out The output file stream
    */
    void save_layer(
        index_t count,
        index_t offset,
        const GeoModelElement& layer,
        std::ostream& out )
    {
        out << "LAYER " << layer.name() << " " << std::endl ;
        index_t it = 0 ;

        for( index_t i = 0; i < layer.nb_children(); ++i ) {
            out << "  " << layer.child_id( i ).index + offset + 1 ;
            it++ ;
            if( it == 5 ) {
                out << std::endl ;
                it = 0 ;
            }
        }
        out << "  0" << std::endl ;
    }

    /*!
    * @brief Write basic header for Gocad coordinate system.
    * @param[in,out] out Output .ml file stream
    */
    void save_coordinate_system( std::ostream& out )
    {
        out << "GOCAD_ORIGINAL_COORDINATE_SYSTEM" << std::endl << "NAME Default"
            << std::endl << "AXIS_NAME \"X\" \"Y\" \"Z\"" << std::endl
            << "AXIS_UNIT \"m\" \"m\" \"m\"" << std::endl << "ZPOSITIVE Elevation"
            << std::endl << "END_ORIGINAL_COORDINATE_SYSTEM" << std::endl ;
    }

    /*!
    * @brief Check if the model can be saved in a skua-gocad .ml file
    * @details It assumes that the model is valid and verifies that:
    *   - all Interfaces have a name and geological feature
    *   - all Surfaces are in an Interface
    *   - all Surfaces are triangulated
    *   - all Regions have a name
    */
    bool check_gocad_validity( const GeoModel& M )
    {
        if( M.nb_interfaces() == 0 ) {
            GEO::Logger::err( "" ) << " The GeoModel " << M.name()
                << " has no Interface" << std::endl ;
            return false ; 
        }
        for( index_t i = 0; i < M.nb_interfaces(); ++i ) {
            const GME& E = M.one_interface( i ) ;
            if( !E.has_name() ) {
                GEO::Logger::err( "" ) << E.gme_id()
                    << " has no name" << std::endl ;
                return false ;
            }
            if( !E.has_geological_feature() ) {
                GEO::Logger::err( "" ) << E.gme_id()
                    << " has no geological feature" << std::endl ;
                return false ;
            }
        }
        for( index_t s = 0; s < M.nb_surfaces(); ++s ) {
            const Surface& S = M.surface( s ) ;
            if( !S.has_parent() ) {
                GEO::Logger::err( "" ) << S.gme_id()
                    << " does not belong to any Interface of the model" << std::endl ;
                return false ;
            }
            if( !S.is_triangulated() ) {
                GEO::Logger::err( "" ) << S.gme_id()
                    << " is not triangulated " << std::endl ;
                return false ;
            }
        }
        for( index_t r = 0; r < M.nb_regions(); ++r ) {
            const Region& R = M.region( r ) ;
            if( !R.has_name() ) {
                GEO::Logger::err( "" ) << R.gme_id()
                    << " has no name" << std::endl ;
                return false ;
            }
        }
        return true ;
    }

    /*!
    * @brief Save the model in a .ml file if it can
    * @param[in] M the model to save
    * @param[in,out] out Output file stream
    * @return false if the model is not compatible with a Gocad model
    */
    bool save_gocad_model3d( const GeoModel& M, std::ostream& out )
    {
        if( !check_gocad_validity( M ) ) {
            GEO::Logger::err( "" ) << " The GeoModel " << M.name()
                << " cannot be saved in .ml format " << std::endl ;
            return false ;
        }
        out.precision( 16 ) ;

        // Gocad Model3d headers
        out << "GOCAD Model3d 1" << std::endl << "HEADER {" << std::endl << "name: "
            << M.name() << std::endl << "}" << std::endl ;

        save_coordinate_system( out ) ;

        // Gocad::TSurf = RINGMesh::Interface 
        for( index_t i = 0; i < M.nb_interfaces(); ++i ) {
            out << "TSURF " << M.one_interface( i ).name() << std::endl ;
        }

        index_t count = 1 ;

        // Gocad::TFace = RINGMesh::Surface 
        for( index_t i = 0; i < M.nb_surfaces(); ++i ) {
            const Surface& s = M.surface( i ) ;
            out << "TFACE " << count << "  " ;
            out << GME::geol_name( s.geological_feature() ) ;
            out << " " << s.parent().name() << std::endl ;

            // Print the key facet which is the first three
            // vertices of the first facet
            out << "  " << s.vertex( 0, 0 ) << std::endl ;
            out << "  " << s.vertex( 0, 1 ) << std::endl ;
            out << "  " << s.vertex( 0, 2 ) << std::endl ;

            ++count ;
        }
        // Universe
        index_t offset_layer = count ;
        save_region( count, M.universe(), out ) ;
        ++count ;
        // Regions
        for( index_t i = 0; i < M.nb_regions(); ++i ) {
            save_region( count, M.region( i ), out ) ;
            ++count ;
        }
        // Layers
        for( index_t i = 0; i < M.nb_layers(); ++i ) {
            save_layer( count, offset_layer, M.layer( i ), out ) ;
            ++count ;
        }
        out << "END" << std::endl ;

        // Save the geometry of the Surfaces, Interface per Interface
        for( index_t i = 0; i < M.nb_interfaces(); ++i ) {
            const GME& tsurf = M.one_interface( i ) ;
            // TSurf beginning header 
            out << "GOCAD TSurf 1" << std::endl << "HEADER {" << std::endl << "name:"
                << tsurf.name() << std::endl << "name_in_model_list:" << tsurf.name()
                << std::endl << "}" << std::endl ;
            save_coordinate_system( out ) ;

            out << "GEOLOGICAL_FEATURE " << tsurf.name() << std::endl
                << "GEOLOGICAL_TYPE " ;
            out << GME::geol_name( tsurf.geological_feature() ) ;
            out << std::endl ;
            out << "PROPERTY_CLASS_HEADER Z {" << std::endl << "is_z:on" << std::endl
                << "}" << std::endl ;
            
            index_t vertex_count = 1 ;
            // TFace vertex index = Surface vertex index + offset 
            index_t offset = vertex_count ;

            // To collect Corners(BStones) indexes 
            // and boundary (Line) first and second vertex indexes
            std::set< index_t > corners ;
            std::set< std::pair<index_t, index_t> > lineindices ;
            for( index_t j = 0; j < tsurf.nb_children(); ++j ) {
                offset = vertex_count ;
                const Surface& S = dynamic_cast<
                    const Surface& >( tsurf.child(j) ) ;

                out << "TFACE" << std::endl ;
                for( index_t k = 0; k < S.nb_vertices(); ++k ) {
                    out << "VRTX " << vertex_count << " " << S.vertex( k )
                        << std::endl ;
                    vertex_count++ ;
                }
                for( index_t k = 0; k < S.nb_cells(); ++k ) {
                    out << "TRGL " << S.surf_vertex_id( k, 0 ) + offset << " "
                        << S.surf_vertex_id( k, 1 ) + offset << " "
                        << S.surf_vertex_id( k, 2 ) + offset << std::endl ;
                }
                for( index_t k = 0; k < S.nb_boundaries(); ++k ) {
                    const Line& L = dynamic_cast<const Line&>( S.boundary( k ) ) ;
                    lineindices.insert( std::pair< index_t, index_t>(
                        S.surf_vertex_id( L.model_vertex_id( 0 ) )+offset,
                        S.surf_vertex_id( L.model_vertex_id( 1 ) )+offset ) ) ;

                    const Corner& c0 = dynamic_cast<const Corner&>( L.boundary( 0 ) ) ;
                    corners.insert( S.surf_vertex_id( c0.model_vertex_id() )+offset ) ;
                    const Corner& c1 = dynamic_cast<const Corner&>( L.boundary( 1 ) ) ;                    
                    corners.insert( S.surf_vertex_id( c1.model_vertex_id() )+offset ) ;                  
                }
            }
            // Add the remaining bstones that are not already in bstones
            for( std::set<index_t>::iterator it( corners.begin() ) ;
                 it != corners.end(); ++it ){                
                out << "BSTONE " << *it << std::endl ;
            }
            for( std::set< std::pair<index_t, index_t> >::iterator
                 it( lineindices.begin() ); it != lineindices.end(); ++it 
            ) {
                out << "BORDER " << vertex_count << " " << it->first << " "
                    << it->second << std::endl ;
                vertex_count++ ;
            }
            out << "END" << std::endl ;
        }
        return true ;
    }

    /*! To save the attributes in a Graphite readable file, we need to write the correct
    * keyword for the attribute type - We restrict ourselves to the 3 types
    * int          "integer"
    * double       "real"
    * float        "real"
    * bool         "boolean"
    */
    inline std::string alias_name( const std::string& in )
    {
        if( in == "int" ) {
            return "integer" ;
        } else if( in == "index" ) {
            return "integer" ;
        } else if( in == "double" ) {
            return "real" ;
        } else if( in == "float" ) {
            return "real" ;
        } else if( in == "bool" ) {
            return "boolean" ;
        }
        ringmesh_assert_not_reached;
        return "" ;
    }

    /*!
    * @brief Write in the out stream things to save for CONTACT, INTERFACE and LAYERS
    */
    void save_high_level_bme( std::ofstream& out, const GeoModelElement& E )
    {
        /// First line:  TYPE - ID - NAME - GEOL
        out << E.gme_id() << " " ;
        if( E.has_name() ) {
            out << E.name() << " " ;
        } else {
            out << "no_name " ;
        }
        out << GeoModelElement::geol_name( E.geological_feature() ) << std::endl ;

        /// Second line:  IDS of children
        for( index_t j = 0; j < E.nb_children(); ++j ) {
            out << " " << E.child_id( j ).index ;
        }
        out << std::endl ;
    }

    /*!
    * @brief Save the GeoModel into a dedicated format bm
    * @todo Write the description of the BM format
    * @todo We need a generic read/write for the attributes !!
    */
    void save_bm_file( const GeoModel& M, const std::string& file_name )
    {
        std::ofstream out( file_name.c_str() ) ;
        if( out.bad() ) {
            GEO::Logger::err( "I/O" ) << "Error when opening the file: "
                << file_name.c_str() << std::endl ;
            return ;
        }
        out.precision( 16 ) ;

        out << "RINGMESH BOUNDARY MODEL" << std::endl ;
        out << "NAME " << M.name() << std::endl ;

        // Numbers of the different types of elements
        for( index_t i = GME::CORNER; i < GME::NO_TYPE; i++ ) {
            GME::TYPE type = static_cast< GME::TYPE >( i ) ;
            out << "NB_" << GME::type_name( type ) << " " << M.nb_elements( type )
                << std::endl ;
        }
        // Write high-level elements
        for( index_t i = GME::CONTACT; i < GME::NO_TYPE; i++ ) {
            GME::TYPE type = static_cast< GME::TYPE >( i ) ;
            index_t nb = M.nb_elements( type ) ;
            for( index_t j = 0; j < nb; ++j ) {
                save_high_level_bme( out, M.element( GME::gme_t( type, j ) ) ) ;
            }
        }
        // Regions
        for( index_t i = 0; i < M.nb_regions(); ++i ) {
            const Region& E = M.region( i ) ;
            // Save ID - NAME
            out << E.gme_id() << " " ;
            if( E.has_name() ) {
                out << E.name() ;
            } else {
                out << "no_name" ;
            }
            out << std::endl ;
            // Second line Signed ids of boundary surfaces
            for( index_t j = 0; j < E.nb_boundaries(); ++j ) {
                if( E.side( j ) ) {
                    out << "+" ;
                } else {
                    out << "-" ;
                }
                out << E.boundary_id( j ).index << " " ;
            }
            out << std::endl ;
        }
        // Universe
        out << "UNIVERSE " << std::endl ;
        for( index_t j = 0; j < M.universe().nb_boundaries(); ++j ) {
            if( M.universe().side( j ) ) {
                out << "+" ;
            } else {
                out << "-" ;
            }
            out << M.universe().boundary_id( j ).index << " " ;
        }
        out << std::endl ;

        /// @todo Review: delete commented code [AB]
        //        // Vertices and attributes on vertices
        //        out << "MODEL_VERTICES" << " " << nb_vertices() << std::endl ;
        //        out << "MODEL_VERTEX_ATTRIBUTES " ;
        //        std::vector< SerializedAttribute< VERTEX > > vertex_attribs ;
        //        get_serializable_attributes( &vertex_attribute_manager_, vertex_attribs, out ) ;
        //        for( index_t i = 0; i < nb_vertices(); ++i ) {
        //            out << vertex( i )  << "  " ;
        //            serialize_write_attributes( out, i, vertex_attribs ) ;
        //            out << std::endl ;
        //        }

        // Corners
        for( index_t i = 0; i < M.nb_corners(); ++i ) {
            out << M.corner( i ).gme_id() << " " << M.corner( i ).vertex()
                << std::endl ;
        }
        // Lines
        for( index_t i = 0; i < M.nb_lines(); ++i ) {
            const Line& L = M.line( i ) ;
            out << L.gme_id() << std::endl ;
            out << "LINE_VERTICES " << L.nb_vertices() << std::endl ;
            for( index_t j = 0; j < L.nb_vertices(); ++j ) {
                out << L.vertex( j ) << std::endl ;
            }
            //            out << "LINE_VERTEX_ATTRIBUTES " ;
            //            std::vector< SerializedAttribute< BME::VERTEX > > line_vertex_attribs ;
            //            get_serializable_attributes(
            //                L.vertex_attribute_manager(), line_vertex_attribs, out ) ;
            //            for( index_t j = 0; j < L.nb_vertices(); ++j ) {
            //                out << j << "  " ;
            //                serialize_write_attributes( out, j, line_vertex_attribs ) ;
            //                out << std::endl ;
            //            }
            //            out << "LINE_SEGMENT_ATTRIBUTES " ;
            //            std::vector< SerializedAttribute< BME::FACET > > line_segments_attribs ;
            //            get_serializable_attributes(
            //                L.facet_attribute_manager(), line_segments_attribs, out ) ;
            //            if( line_segments_attribs.size() > 0 ) {
            //                for( index_t j = 0; j < L.nb_cells(); ++j ) {
            //                    out << j << "  " ;
            //                    serialize_write_attributes( out, j, line_segments_attribs ) ;
            //                    out << std::endl ;
            //                }
            //            }
            out << "IN_BOUNDARY " ;
            for( index_t j = 0; j < L.nb_in_boundary(); ++j ) {
                out << L.in_boundary_id( j ).index << " " ;
            }
            out << std::endl ;
        }

        // Surfaces
        for( index_t i = 0; i < M.nb_surfaces(); ++i ) {
            const Surface& S = M.surface( i ) ;
            out << S.gme_id() << std::endl ;
            out << "SURFACE_VERTICES " << S.nb_vertices() << std::endl ;
            for( index_t j = 0; j < S.nb_vertices(); ++j ) {
                out << S.vertex( j ) << std::endl ;
            }
            //            out << "SURFACE_VERTEX_ATTRIBUTES " ;
            //            std::vector< SerializedAttribute< BME::VERTEX > > surface_vertex_attribs ;
            //            get_serializable_attributes(
            //                S.vertex_attribute_manager(), surface_vertex_attribs, out ) ;
            //            for( index_t j = 0; j < S.nb_vertices(); ++j ) {
            //                out << j << "  " ;
            //                serialize_write_attributes( out, j, surface_vertex_attribs ) ;
            //                out << std::endl ;
            //            }

            out << "SURFACE_CORNERS " << S.nb_corners() << std::endl ;
            out << "SURFACE_FACETS " << S.nb_cells() << std::endl ;
            //            out << "SURFACE_FACET_ATTRIBUTES " ;
            //            std::vector< SerializedAttribute< BME::FACET > > surface_facet_attribs ;
            //            get_serializable_attributes(
            //                S.facet_attribute_manager(), surface_facet_attribs, out ) ;
            //
            for( index_t j = 0; j < S.nb_cells(); ++j ) {
                out << S.nb_vertices_in_facet( j ) << " " ;
                for( index_t v = 0; v < S.nb_vertices_in_facet( j ); ++v ) {
                    out << S.surf_vertex_id( j, v ) << " " ;
                }
                //                serialize_write_attributes( out, j, surface_facet_attribs ) ;
                out << std::endl ;
            }
        }
    }

    /*!
    * @brief Save the model in smesh format
    * @details No attributes and no boundary marker are transferred
    * @todo Test this function - Create the appropriate handler
    */
    void save_smesh_file( const GeoModel& M, const std::string& file_name )
    {
        std::ofstream out( file_name.c_str() ) ;
        if( out.bad() ) {
            GEO::Logger::err( "I/O" ) << "Error when opening the file: "
                << file_name.c_str() << std::endl ;
            return ;
        }
        out.precision( 16 ) ;

        /// 1. Write the unique vertices
        out << "# Node list" << std::endl ;
        out << "# node count, 3 dim, no attribute, no boundary marker" << std::endl ;
        out << M.mesh.vertices.nb() << " 3 0 0" << std::endl ;
        out << "# node index, node coordinates " << std::endl ;
        for( index_t p = 0; p < M.mesh.vertices.nb(); p++ ) {
            const vec3& V = M.mesh.vertices.vertex( p ) ;
            out << p << " " << " " << V.x << " " << V.y << " " << V.z << std::endl ;
        }

        /// 2. Write the triangles
        out << "# Part 2 - facet list" << std::endl ;
        out << "# facet count, no boundary marker" << std::endl ;
        out << nb_facets( M ) << "  0 " << std::endl ;

        for( index_t i = 0; i < M.nb_surfaces(); ++i ) {
            const Surface& S = M.surface( i ) ;
            for( index_t f = 0; f < S.nb_cells(); f++ ) {
                out << S.nb_vertices_in_facet( f ) << " " ;
                for( index_t v = 0; v < S.nb_vertices_in_facet( f ); v++ ) {
                    out << S.model_vertex_id( f, v ) << " " ;
                }
                out << std::endl ;
            }
        }

        // Do not forget the stupid zeros at the end of the file
        out << std::endl << "0" << std::endl << "0" << std::endl ;
    }

    static double read_double( GEO::LineInput& in, index_t field )
    {
        double result ;
        std::istringstream iss( in.field( field ) ) ;
        iss >> result >> std::ws ;
        return result ;
    }

}

namespace RINGMesh {

    /************************************************************************/

    class MLIOHandler: public GeoModelSurfaceIOHandler {
    public:
        /*! Load a .ml (Gocad file)
         * @pre Filename is valid
         */
        virtual bool load( const std::string& filename, GeoModel& model )
        {
            std::ifstream input( filename.c_str() ) ;
            if( input ) {
                GeoModelBuilderGocad builder( model, filename ) ;

                time_t start_load, end_load ;
                time( &start_load ) ;

                if( builder.build_model() ) {
                    print_model( model ) ;
                    // Check validity
                    RINGMesh::is_geomodel_valid( model ) ;

                    time( &end_load ) ;
                    GEO::Logger::out( "I/O" )
                        << " Loaded model " << model.name() << " from " << std::endl 
                        << filename << " timing: " 
                        << difftime( end_load, start_load ) << "sec" << std::endl ;
                    return true ;
                }
            }          
            GEO::Logger::out( "I/O" )
                << "Failed loading model from file "
                << filename << std::endl ;
            return false ;
        }

        virtual bool save( const GeoModel& model, const std::string& filename )
        {
            std::ofstream out( filename.c_str() ) ;
            return save_gocad_model3d( model, out ) ;
        }
    } ;

    class BMIOHandler: public GeoModelSurfaceIOHandler {
    public:
        virtual bool load( const std::string& filename, GeoModel& model )
        {           
            std::ifstream input( filename.c_str() ) ;
            if( input ) {
                GeoModelBuilderBM builder( model, filename ) ;
                if( builder.build_model() ) {
                    GEO::Logger::out( "I/O" )
                        << " Loaded model " << model.name() << " from "
                        << filename << std::endl ;
                    print_model( model ) ;
                    RINGMesh::is_geomodel_valid( model ) ;                
                    return true ;
                }
            }
            GEO::Logger::out( "I/O" )
                << "Failed loading geological model from file "
                << filename << std::endl ;
            return false ;
        }

        virtual bool save( const GeoModel& model, const std::string& filename )
        {
            save_bm_file( model, filename ) ;
            return true ;
        }
    } ;

    class UCDIOHandler: public GeoModelSurfaceIOHandler {
    public:
        virtual bool load( const std::string& filename, GeoModel& model )
        {
            GEO::Logger::err( "I/O" )
                << "Geological model loading of a from UCD mesh not yet implemented"
                << std::endl ;
            return false ;
        }

        virtual bool save( const GeoModel& model, const std::string& filename )
        {
            std::string full_path ;
            std::string directory ;
            create_directory_from_filename( filename, full_path, directory ) ;

            std::ostringstream oss_cmd ;
            oss_cmd << full_path << "/cmd.lgi" ;
            std::ofstream cmd( oss_cmd.str().c_str() ) ;
            cmd << "cmo/create/3DMesh" << std::endl ;
            for( index_t s = 0; s < model.nb_surfaces(); s++ ) {
                std::ostringstream oss ;
                oss << full_path << "/surface_" << s << ".inp" ;
                std::ofstream out( oss.str().c_str() ) ;
                out.precision( 16 ) ;

                const Surface& surface = model.surface( s ) ;
                out << surface.nb_vertices() << " " << surface.nb_cells() << " 0 0 0"
                    << std::endl ;
                for( index_t v = 0; v < surface.nb_vertices(); v++ ) {
                    out << v << " " << surface.vertex( v ) << std::endl ;
                }

                for( index_t f = 0; f < surface.nb_cells(); f++ ) {
                    out << f << " 0 tri" ;
                    for( index_t v = 0; v < surface.nb_vertices_in_facet( f );
                        v++ ) {
                        out << " " << surface.surf_vertex_id( f, v ) ;
                    }
                    out << std::endl ;
                }

                cmd << "cmo/create/s_" << s << std::endl ;
                cmd << "read/surface_" << s << ".inp/s_" << s << std::endl ;
                cmd << "surface/surface_" << s << "/" ;
                if( surface.is_on_voi() ) {
                    cmd << "reflect" ;
                } else {
                    cmd << "interface" ;
                }
                cmd << "/sheet/s_" << s << std::endl ;
            }

            for( index_t r = 0; r < model.nb_regions(); r++ ) {
                const Region& region = model.region( r ) ;
                cmd << "region/" << region.name() << "/" ;
                std::string sep = "" ;
                for( index_t s = 0; s < region.nb_boundaries(); s++ ) {
                    cmd << sep << "&" << std::endl ;
                    if( region.side( s ) )
                        cmd << "g" ;
                    else
                        cmd << "l" ;
                    cmd << "e surface_" << region.boundary_id( s ).index ;
                    sep = " and " ;
                }
                cmd << std::endl ;
                cmd << "mregion/mat" << r << "/" << region.name() << std::endl ;
            }

            cmd << "finish" << std::endl ;
            return true ;
        }
    } ;

    class WebGLIOHandler: public GeoModelSurfaceIOHandler {
    public:
        void print_KeyboardState( std::ofstream& out )
        {
            out << "/**" << std::endl ;
            out << " * @author Lee Stemkoski" << std::endl ;
            out << " *" << std::endl ;
            out << " * Usage: " << std::endl ;
            out << " * (1) create a global variable:" << std::endl ;
            out << " *      var keyboard = new KeyboardState();" << std::endl ;
            out << " * (2) during main loop:" << std::endl ;
            out << " *       keyboard.update();" << std::endl ;
            out << " * (3) check state of keys:" << std::endl ;
            out
                << " *       keyboard.down(\"A\")    -- true for one update cycle after key is pressed"
                << std::endl ;
            out
                << " *       keyboard.pressed(\"A\") -- true as long as key is being pressed"
                << std::endl ;
            out
                << " *       keyboard.up(\"A\")      -- true for one update cycle after key is released"
                << std::endl ;
            out << " * " << std::endl ;
            out
                << " *  See KeyboardState.k object data below for names of keys whose state can be polled"
                << std::endl ;
            out << " */" << std::endl ;
            out << " " << std::endl ;
            out << "// initialization" << std::endl ;
            out << "KeyboardState = function()" << std::endl ;
            out << "{   " << std::endl ;
            out << "    // bind keyEvents" << std::endl ;
            out
                << "    document.addEventListener(\"keydown\", KeyboardState.onKeyDown, false);"
                << std::endl ;
            out
                << "    document.addEventListener(\"keyup\",   KeyboardState.onKeyUp,   false); "
                << std::endl ;
            out << "}" << std::endl ;
            out << "" << std::endl ;
            out
                << "///////////////////////////////////////////////////////////////////////////////"
                << std::endl ;
            out << "" << std::endl ;
            out << "KeyboardState.k = " << std::endl ;
            out << "{  " << std::endl ;
            out
                << "    8: \"backspace\",  9: \"tab\",       13: \"enter\",    16: \"shift\", "
                << std::endl ;
            out
                << "    17: \"ctrl\",     18: \"alt\",       27: \"esc\",      32: \"space\","
                << std::endl ;
            out
                << "    33: \"pageup\",   34: \"pagedown\",  35: \"end\",      36: \"home\","
                << std::endl ;
            out
                << "    37: \"left\",     38: \"up\",        39: \"right\",    40: \"down\","
                << std::endl ;
            out
                << "    45: \"insert\",   46: \"delete\",   186: \";\",       187: \"=\","
                << std::endl ;
            out
                << "    188: \",\",      189: \"-\",        190: \".\",       191: \"/\","
                << std::endl ;
            out
                << "    219: \"[\",      220: \"\\\\\",       221: \"]\",       222: \"'\""
                << std::endl ;
            out << "}" << std::endl ;
            out << "" << std::endl ;
            out << "KeyboardState.status = {};" << std::endl ;
            out << "" << std::endl ;
            out << "KeyboardState.keyName = function ( keyCode )" << std::endl ;
            out << "{" << std::endl ;
            out << "    return ( KeyboardState.k[keyCode] != null ) ? "
                << std::endl ;
            out << "        KeyboardState.k[keyCode] : " << std::endl ;
            out << "        String.fromCharCode(keyCode);" << std::endl ;
            out << "}" << std::endl ;
            out << "" << std::endl ;
            out << "KeyboardState.onKeyUp = function(event)" << std::endl ;
            out << "{" << std::endl ;
            out << "    var key = KeyboardState.keyName(event.keyCode);"
                << std::endl ;
            out << "    if ( KeyboardState.status[key] )" << std::endl ;
            out << "        KeyboardState.status[key].pressed = false;"
                << std::endl ;
            out << "}" << std::endl ;
            out << "" << std::endl ;
            out << "KeyboardState.onKeyDown = function(event)" << std::endl ;
            out << "{" << std::endl ;
            out << "    var key = KeyboardState.keyName(event.keyCode);"
                << std::endl ;
            out << "    if ( !KeyboardState.status[key] )" << std::endl ;
            out
                << "        KeyboardState.status[key] = { down: false, pressed: false, up: false, updatedPreviously: false };"
                << std::endl ;
            out << "}" << std::endl ;
            out << "" << std::endl ;
            out << "KeyboardState.prototype.update = function()" << std::endl ;
            out << "{" << std::endl ;
            out << "    for (var key in KeyboardState.status)" << std::endl ;
            out << "    {" << std::endl ;
            out
                << "        // insure that every keypress has \"down\" status exactly once"
                << std::endl ;
            out << "        if ( !KeyboardState.status[key].updatedPreviously )"
                << std::endl ;
            out << "        {" << std::endl ;
            out << "            KeyboardState.status[key].down              = true;"
                << std::endl ;
            out << "            KeyboardState.status[key].pressed           = true;"
                << std::endl ;
            out << "            KeyboardState.status[key].updatedPreviously = true;"
                << std::endl ;
            out << "        }" << std::endl ;
            out << "        else // updated previously" << std::endl ;
            out << "        {" << std::endl ;
            out << "            KeyboardState.status[key].down = false;"
                << std::endl ;
            out << "        }" << std::endl ;
            out << "" << std::endl ;
            out << "        // key has been flagged as \"up\" since last update"
                << std::endl ;
            out << "        if ( KeyboardState.status[key].up ) " << std::endl ;
            out << "        {" << std::endl ;
            out << "            delete KeyboardState.status[key];" << std::endl ;
            out << "            continue; // move on to next key" << std::endl ;
            out << "        }" << std::endl ;
            out << "        " << std::endl ;
            out
                << "        if ( !KeyboardState.status[key].pressed ) // key released"
                << std::endl ;
            out << "            KeyboardState.status[key].up = true;" << std::endl ;
            out << "    }" << std::endl ;
            out << "}" << std::endl ;
            out << "" << std::endl ;
            out << "KeyboardState.prototype.down = function(keyName)" << std::endl ;
            out << "{" << std::endl ;
            out
                << "    return (KeyboardState.status[keyName] && KeyboardState.status[keyName].down);"
                << std::endl ;
            out << "}" << std::endl ;
            out << "" << std::endl ;
            out << "KeyboardState.prototype.pressed = function(keyName)"
                << std::endl ;
            out << "{" << std::endl ;
            out
                << "    return (KeyboardState.status[keyName] && KeyboardState.status[keyName].pressed);"
                << std::endl ;
            out << "}" << std::endl ;
            out << "" << std::endl ;
            out << "KeyboardState.prototype.up = function(keyName)" << std::endl ;
            out << "{" << std::endl ;
            out
                << "    return (KeyboardState.status[keyName] && KeyboardState.status[keyName].up);"
                << std::endl ;
            out << "}" << std::endl ;
            out << "" << std::endl ;
            out << "KeyboardState.prototype.debug = function()" << std::endl ;
            out << "{" << std::endl ;
            out << "    var list = \"Keys active: \";" << std::endl ;
            out << "    for (var arg in KeyboardState.status)" << std::endl ;
            out << "        list += \" \" + arg" << std::endl ;
            out << "    console.log(list);" << std::endl ;
            out << "}" << std::endl ;
            out << "" << std::endl ;
            out
                << "///////////////////////////////////////////////////////////////////////////////"
                << std::endl ;
        }

        void print_OrbitControls( std::ofstream& out )
        {
            out << "/**" << std::endl ;
            out << " * @author qiao / https://github.com/qiao" << std::endl ;
            out << " * @author mrdoob / http://mrdoob.com" << std::endl ;
            out << " * @author alteredq / http://alteredqualia.com/" << std::endl ;
            out << " * @author WestLangley / http://github.com/WestLangley"
                << std::endl ;
            out << " * @author erich666 / http://erichaines.com" << std::endl ;
            out << " */" << std::endl ;
            out << "/*global THREE, console */" << std::endl ;
            out << "" << std::endl ;
            out
                << "// This set of controls performs orbiting, dollying (zooming), and panning. It maintains"
                << std::endl ;
            out
                << "// the \"up\" direction as +Y, unlike the TrackballControls. Touch on tablet and phones is"
                << std::endl ;
            out << "// supported." << std::endl ;
            out << "//" << std::endl ;
            out << "//    Orbit - left mouse / touch: one finger move" << std::endl ;
            out
                << "//    Zoom - middle mouse, or mousewheel / touch: two finger spread or squish"
                << std::endl ;
            out
                << "//    Pan - right mouse, or arrow keys / touch: three finter swipe"
                << std::endl ;
            out << "" << std::endl ;
            out << "THREE.OrbitControls = function ( object, domElement ) {"
                << std::endl ;
            out << "" << std::endl ;
            out << "    this.object = object;" << std::endl ;
            out
                << "    this.domElement = ( domElement !== undefined ) ? domElement : document;"
                << std::endl ;
            out << "" << std::endl ;
            out << "    // API" << std::endl ;
            out << "" << std::endl ;
            out << "    // Set to false to disable this control" << std::endl ;
            out << "    this.enabled = true;" << std::endl ;
            out << "" << std::endl ;
            out
                << "    // \"target\" sets the location of focus, where the control orbits around"
                << std::endl ;
            out << "    // and where it pans with respect to." << std::endl ;
            out << "    this.target = new THREE.Vector3();" << std::endl ;
            out << "" << std::endl ;
            out << "    // center is old, deprecated; use \"target\" instead"
                << std::endl ;
            out << "    this.center = this.target;" << std::endl ;
            out << "" << std::endl ;
            out
                << "    // This option actually enables dollying in and out; left as \"zoom\" for"
                << std::endl ;
            out << "    // backwards compatibility" << std::endl ;
            out << "    this.noZoom = false;" << std::endl ;
            out << "    this.zoomSpeed = 1.0;" << std::endl ;
            out << "" << std::endl ;
            out
                << "    // Limits to how far you can dolly in and out ( PerspectiveCamera only )"
                << std::endl ;
            out << "    this.minDistance = 0;" << std::endl ;
            out << "    this.maxDistance = Infinity;" << std::endl ;
            out << "" << std::endl ;
            out
                << "    // Limits to how far you can zoom in and out ( OrthographicCamera only )"
                << std::endl ;
            out << "    this.minZoom = 0;" << std::endl ;
            out << "    this.maxZoom = Infinity;" << std::endl ;
            out << "" << std::endl ;
            out << "    // Set to true to disable this control" << std::endl ;
            out << "    this.noRotate = false;" << std::endl ;
            out << "    this.rotateSpeed = 1.0;" << std::endl ;
            out << "" << std::endl ;
            out << "    // Set to true to disable this control" << std::endl ;
            out << "    this.noPan = false;" << std::endl ;
            out << "    this.keyPanSpeed = 7.0; // pixels moved per arrow key push"
                << std::endl ;
            out << "" << std::endl ;
            out << "    // Set to true to automatically rotate around the target"
                << std::endl ;
            out << "    this.autoRotate = false;" << std::endl ;
            out
                << "    this.autoRotateSpeed = 2.0; // 30 seconds per round when fps is 60"
                << std::endl ;
            out << "" << std::endl ;
            out << "    // How far you can orbit vertically, upper and lower limits."
                << std::endl ;
            out << "    // Range is 0 to Math.PI radians." << std::endl ;
            out << "    this.minPolarAngle = 0; // radians" << std::endl ;
            out << "    this.maxPolarAngle = Math.PI; // radians" << std::endl ;
            out << "" << std::endl ;
            out
                << "    // How far you can orbit horizontally, upper and lower limits."
                << std::endl ;
            out
                << "    // If set, must be a sub-interval of the interval [ - Math.PI, Math.PI ]."
                << std::endl ;
            out << "    this.minAzimuthAngle = - Infinity; // radians" << std::endl ;
            out << "    this.maxAzimuthAngle = Infinity; // radians" << std::endl ;
            out << "" << std::endl ;
            out << "    // Set to true to disable use of the keys" << std::endl ;
            out << "    this.noKeys = false;" << std::endl ;
            out << "" << std::endl ;
            out << "    // The four arrow keys" << std::endl ;
            out << "    this.keys = { LEFT: 37, UP: 38, RIGHT: 39, BOTTOM: 40 };"
                << std::endl ;
            out << "" << std::endl ;
            out << "    // Mouse buttons" << std::endl ;
            out
                << "    this.mouseButtons = { ORBIT: THREE.MOUSE.LEFT, ZOOM: THREE.MOUSE.MIDDLE, PAN: THREE.MOUSE.RIGHT };"
                << std::endl ;
            out << "" << std::endl ;
            out << "    ////////////" << std::endl ;
            out << "    // internals" << std::endl ;
            out << "" << std::endl ;
            out << "    var scope = this;" << std::endl ;
            out << "" << std::endl ;
            out << "    var EPS = 0.000001;" << std::endl ;
            out << "" << std::endl ;
            out << "    var rotateStart = new THREE.Vector2();" << std::endl ;
            out << "    var rotateEnd = new THREE.Vector2();" << std::endl ;
            out << "    var rotateDelta = new THREE.Vector2();" << std::endl ;
            out << "" << std::endl ;
            out << "    var panStart = new THREE.Vector2();" << std::endl ;
            out << "    var panEnd = new THREE.Vector2();" << std::endl ;
            out << "    var panDelta = new THREE.Vector2();" << std::endl ;
            out << "    var panOffset = new THREE.Vector3();" << std::endl ;
            out << "" << std::endl ;
            out << "    var offset = new THREE.Vector3();" << std::endl ;
            out << "" << std::endl ;
            out << "    var dollyStart = new THREE.Vector2();" << std::endl ;
            out << "    var dollyEnd = new THREE.Vector2();" << std::endl ;
            out << "    var dollyDelta = new THREE.Vector2();" << std::endl ;
            out << "" << std::endl ;
            out << "    var theta;" << std::endl ;
            out << "    var phi;" << std::endl ;
            out << "    var phiDelta = 0;" << std::endl ;
            out << "    var thetaDelta = 0;" << std::endl ;
            out << "    var scale = 1;" << std::endl ;
            out << "    var pan = new THREE.Vector3();" << std::endl ;
            out << "" << std::endl ;
            out << "    var lastPosition = new THREE.Vector3();" << std::endl ;
            out << "    var lastQuaternion = new THREE.Quaternion();" << std::endl ;
            out << "" << std::endl ;
            out
                << "    var STATE = { NONE : -1, ROTATE : 0, DOLLY : 1, PAN : 2, TOUCH_ROTATE : 3, TOUCH_DOLLY : 4, TOUCH_PAN : 5 };"
                << std::endl ;
            out << "" << std::endl ;
            out << "    var state = STATE.NONE;" << std::endl ;
            out << "" << std::endl ;
            out << "    // for reset" << std::endl ;
            out << "" << std::endl ;
            out << "    this.target0 = this.target.clone();" << std::endl ;
            out << "    this.position0 = this.object.position.clone();"
                << std::endl ;
            out << "    this.zoom0 = this.object.zoom;" << std::endl ;
            out << "" << std::endl ;
            out << "    // so camera.up is the orbit axis" << std::endl ;
            out << "" << std::endl ;
            out
                << "    var quat = new THREE.Quaternion().setFromUnitVectors( object.up, new THREE.Vector3( 0, 1, 0 ) );"
                << std::endl ;
            out << "    var quatInverse = quat.clone().inverse();" << std::endl ;
            out << "" << std::endl ;
            out << "    // events" << std::endl ;
            out << "" << std::endl ;
            out << "    var changeEvent = { type: 'change' };" << std::endl ;
            out << "    var startEvent = { type: 'start' };" << std::endl ;
            out << "    var endEvent = { type: 'end' };" << std::endl ;
            out << "" << std::endl ;
            out << "    this.rotateLeft = function ( angle ) {" << std::endl ;
            out << "" << std::endl ;
            out << "        if ( angle === undefined ) {" << std::endl ;
            out << "" << std::endl ;
            out << "            angle = getAutoRotationAngle();" << std::endl ;
            out << "" << std::endl ;
            out << "        }" << std::endl ;
            out << "" << std::endl ;
            out << "        thetaDelta -= angle;" << std::endl ;
            out << "" << std::endl ;
            out << "    };" << std::endl ;
            out << "" << std::endl ;
            out << "    this.rotateUp = function ( angle ) {" << std::endl ;
            out << "" << std::endl ;
            out << "        if ( angle === undefined ) {" << std::endl ;
            out << "" << std::endl ;
            out << "            angle = getAutoRotationAngle();" << std::endl ;
            out << "" << std::endl ;
            out << "        }" << std::endl ;
            out << "" << std::endl ;
            out << "        phiDelta -= angle;" << std::endl ;
            out << "" << std::endl ;
            out << "    };" << std::endl ;
            out << "" << std::endl ;
            out << "    // pass in distance in world space to move left"
                << std::endl ;
            out << "    this.panLeft = function ( distance ) {" << std::endl ;
            out << "" << std::endl ;
            out << "        var te = this.object.matrix.elements;" << std::endl ;
            out << "" << std::endl ;
            out << "        // get X column of matrix" << std::endl ;
            out << "        panOffset.set( te[ 0 ], te[ 1 ], te[ 2 ] );"
                << std::endl ;
            out << "        panOffset.multiplyScalar( - distance );" << std::endl ;
            out << "" << std::endl ;
            out << "        pan.add( panOffset );" << std::endl ;
            out << "" << std::endl ;
            out << "    };" << std::endl ;
            out << "" << std::endl ;
            out << "    // pass in distance in world space to move up" << std::endl ;
            out << "    this.panUp = function ( distance ) {" << std::endl ;
            out << "" << std::endl ;
            out << "        var te = this.object.matrix.elements;" << std::endl ;
            out << "" << std::endl ;
            out << "        // get Y column of matrix" << std::endl ;
            out << "        panOffset.set( te[ 4 ], te[ 5 ], te[ 6 ] );"
                << std::endl ;
            out << "        panOffset.multiplyScalar( distance );" << std::endl ;
            out << "" << std::endl ;
            out << "        pan.add( panOffset );" << std::endl ;
            out << "" << std::endl ;
            out << "    };" << std::endl ;
            out << "" << std::endl ;
            out << "    // pass in x,y of change desired in pixel space,"
                << std::endl ;
            out << "    // right and down are positive" << std::endl ;
            out << "    this.pan = function ( deltaX, deltaY ) {" << std::endl ;
            out << "" << std::endl ;
            out
                << "        var element = scope.domElement === document ? scope.domElement.body : scope.domElement;"
                << std::endl ;
            out << "" << std::endl ;
            out << "        if ( scope.object instanceof THREE.PerspectiveCamera ) {"
                << std::endl ;
            out << "" << std::endl ;
            out << "            // perspective" << std::endl ;
            out << "            var position = scope.object.position;" << std::endl ;
            out << "            var offset = position.clone().sub( scope.target );"
                << std::endl ;
            out << "            var targetDistance = offset.length();" << std::endl ;
            out << "" << std::endl ;
            out << "            // half of the fov is center to top of screen"
                << std::endl ;
            out
                << "            targetDistance *= Math.tan( ( scope.object.fov / 2 ) * Math.PI / 180.0 );"
                << std::endl ;
            out << "" << std::endl ;
            out
                << "            // we actually don't use screenWidth, since perspective camera is fixed to screen height"
                << std::endl ;
            out
                << "            scope.panLeft( 2 * deltaX * targetDistance / element.clientHeight );"
                << std::endl ;
            out
                << "            scope.panUp( 2 * deltaY * targetDistance / element.clientHeight );"
                << std::endl ;
            out << "" << std::endl ;
            out
                << "        } else if ( scope.object instanceof THREE.OrthographicCamera ) {"
                << std::endl ;
            out << "" << std::endl ;
            out << "            // orthographic" << std::endl ;
            out
                << "            scope.panLeft( deltaX * (scope.object.right - scope.object.left) / element.clientWidth );"
                << std::endl ;
            out
                << "            scope.panUp( deltaY * (scope.object.top - scope.object.bottom) / element.clientHeight );"
                << std::endl ;
            out << "" << std::endl ;
            out << "        } else {" << std::endl ;
            out << "" << std::endl ;
            out << "            // camera neither orthographic or perspective"
                << std::endl ;
            out
                << "            console.warn( 'WARNING: OrbitControls.js encountered an unknown camera type - pan disabled.' );"
                << std::endl ;
            out << "" << std::endl ;
            out << "        }" << std::endl ;
            out << "" << std::endl ;
            out << "    };" << std::endl ;
            out << "" << std::endl ;
            out << "    this.dollyIn = function ( dollyScale ) {" << std::endl ;
            out << "" << std::endl ;
            out << "        if ( dollyScale === undefined ) {" << std::endl ;
            out << "" << std::endl ;
            out << "            dollyScale = getZoomScale();" << std::endl ;
            out << "" << std::endl ;
            out << "        }" << std::endl ;
            out << "" << std::endl ;
            out << "        if ( scope.object instanceof THREE.PerspectiveCamera ) {"
                << std::endl ;
            out << "" << std::endl ;
            out << "            scale /= dollyScale;" << std::endl ;
            out << "" << std::endl ;
            out
                << "        } else if ( scope.object instanceof THREE.OrthographicCamera ) {"
                << std::endl ;
            out << "" << std::endl ;
            out
                << "            scope.object.zoom = Math.max( this.minZoom, Math.min( this.maxZoom, this.object.zoom * dollyScale ) );"
                << std::endl ;
            out << "            scope.object.updateProjectionMatrix();"
                << std::endl ;
            out << "            scope.dispatchEvent( changeEvent );" << std::endl ;
            out << "" << std::endl ;
            out << "        } else {" << std::endl ;
            out << "" << std::endl ;
            out
                << "            console.warn( 'WARNING: OrbitControls.js encountered an unknown camera type - dolly/zoom disabled.' );"
                << std::endl ;
            out << "" << std::endl ;
            out << "        }" << std::endl ;
            out << "" << std::endl ;
            out << "    };" << std::endl ;
            out << "" << std::endl ;
            out << "    this.dollyOut = function ( dollyScale ) {" << std::endl ;
            out << "" << std::endl ;
            out << "        if ( dollyScale === undefined ) {" << std::endl ;
            out << "" << std::endl ;
            out << "            dollyScale = getZoomScale();" << std::endl ;
            out << "" << std::endl ;
            out << "        }" << std::endl ;
            out << "" << std::endl ;
            out << "        if ( scope.object instanceof THREE.PerspectiveCamera ) {"
                << std::endl ;
            out << "" << std::endl ;
            out << "            scale *= dollyScale;" << std::endl ;
            out << "" << std::endl ;
            out
                << "        } else if ( scope.object instanceof THREE.OrthographicCamera ) {"
                << std::endl ;
            out << "" << std::endl ;
            out
                << "            scope.object.zoom = Math.max( this.minZoom, Math.min( this.maxZoom, this.object.zoom / dollyScale ) );"
                << std::endl ;
            out << "            scope.object.updateProjectionMatrix();"
                << std::endl ;
            out << "            scope.dispatchEvent( changeEvent );" << std::endl ;
            out << "" << std::endl ;
            out << "        } else {" << std::endl ;
            out << "" << std::endl ;
            out
                << "            console.warn( 'WARNING: OrbitControls.js encountered an unknown camera type - dolly/zoom disabled.' );"
                << std::endl ;
            out << "" << std::endl ;
            out << "        }" << std::endl ;
            out << "" << std::endl ;
            out << "    };" << std::endl ;
            out << "" << std::endl ;
            out << "    this.update = function () {" << std::endl ;
            out << "" << std::endl ;
            out << "        var position = this.object.position;" << std::endl ;
            out << "" << std::endl ;
            out << "        offset.copy( position ).sub( this.target );"
                << std::endl ;
            out << "" << std::endl ;
            out << "        // rotate offset to \"y-axis-is-up\" space"
                << std::endl ;
            out << "        offset.applyQuaternion( quat );" << std::endl ;
            out << "" << std::endl ;
            out << "        // angle from z-axis around y-axis" << std::endl ;
            out << "" << std::endl ;
            out << "        theta = Math.atan2( offset.x, offset.z );" << std::endl ;
            out << "" << std::endl ;
            out << "        // angle from y-axis" << std::endl ;
            out << "" << std::endl ;
            out
                << "        phi = Math.atan2( Math.sqrt( offset.x * offset.x + offset.z * offset.z ), offset.y );"
                << std::endl ;
            out << "" << std::endl ;
            out << "        if ( this.autoRotate && state === STATE.NONE ) {"
                << std::endl ;
            out << "" << std::endl ;
            out << "            this.rotateLeft( getAutoRotationAngle() );"
                << std::endl ;
            out << "" << std::endl ;
            out << "        }" << std::endl ;
            out << "" << std::endl ;
            out << "        theta += thetaDelta;" << std::endl ;
            out << "        phi += phiDelta;" << std::endl ;
            out << "" << std::endl ;
            out << "        // restrict theta to be between desired limits"
                << std::endl ;
            out
                << "        theta = Math.max( this.minAzimuthAngle, Math.min( this.maxAzimuthAngle, theta ) );"
                << std::endl ;
            out << "" << std::endl ;
            out << "        // restrict phi to be between desired limits"
                << std::endl ;
            out
                << "        phi = Math.max( this.minPolarAngle, Math.min( this.maxPolarAngle, phi ) );"
                << std::endl ;
            out << "" << std::endl ;
            out << "        // restrict phi to be betwee EPS and PI-EPS"
                << std::endl ;
            out << "        phi = Math.max( EPS, Math.min( Math.PI - EPS, phi ) );"
                << std::endl ;
            out << "" << std::endl ;
            out << "        var radius = offset.length() * scale;" << std::endl ;
            out << "" << std::endl ;
            out << "        // restrict radius to be between desired limits"
                << std::endl ;
            out
                << "        radius = Math.max( this.minDistance, Math.min( this.maxDistance, radius ) );"
                << std::endl ;
            out << "" << std::endl ;
            out << "        // move target to panned location" << std::endl ;
            out << "        this.target.add( pan );" << std::endl ;
            out << "" << std::endl ;
            out << "        offset.x = radius * Math.sin( phi ) * Math.sin( theta );"
                << std::endl ;
            out << "        offset.y = radius * Math.cos( phi );" << std::endl ;
            out << "        offset.z = radius * Math.sin( phi ) * Math.cos( theta );"
                << std::endl ;
            out << "" << std::endl ;
            out
                << "        // rotate offset back to \"camera-up-vector-is-up\" space"
                << std::endl ;
            out << "        offset.applyQuaternion( quatInverse );" << std::endl ;
            out << "" << std::endl ;
            out << "        position.copy( this.target ).add( offset );"
                << std::endl ;
            out << "" << std::endl ;
            out << "        this.object.lookAt( this.target );" << std::endl ;
            out << "" << std::endl ;
            out << "        thetaDelta = 0;" << std::endl ;
            out << "        phiDelta = 0;" << std::endl ;
            out << "        scale = 1;" << std::endl ;
            out << "        pan.set( 0, 0, 0 );" << std::endl ;
            out << "" << std::endl ;
            out << "        // update condition is:" << std::endl ;
            out
                << "        // min(camera displacement, camera rotation in radians)^2 > EPS"
                << std::endl ;
            out
                << "        // using small-angle approximation cos(x/2) = 1 - x^2 / 8"
                << std::endl ;
            out << "" << std::endl ;
            out
                << "        if ( lastPosition.distanceToSquared( this.object.position ) > EPS"
                << std::endl ;
            out
                << "            || 8 * (1 - lastQuaternion.dot(this.object.quaternion)) > EPS ) {"
                << std::endl ;
            out << "" << std::endl ;
            out << "            this.dispatchEvent( changeEvent );" << std::endl ;
            out << "" << std::endl ;
            out << "            lastPosition.copy( this.object.position );"
                << std::endl ;
            out << "            lastQuaternion.copy (this.object.quaternion );"
                << std::endl ;
            out << "" << std::endl ;
            out << "        }" << std::endl ;
            out << "" << std::endl ;
            out << "    };" << std::endl ;
            out << "" << std::endl ;
            out << "" << std::endl ;
            out << "    this.reset = function () {" << std::endl ;
            out << "" << std::endl ;
            out << "        state = STATE.NONE;" << std::endl ;
            out << "" << std::endl ;
            out << "        this.target.copy( this.target0 );" << std::endl ;
            out << "        this.object.position.copy( this.position0 );"
                << std::endl ;
            out << "        this.object.zoom = this.zoom0;" << std::endl ;
            out << "" << std::endl ;
            out << "        this.object.updateProjectionMatrix();" << std::endl ;
            out << "        this.dispatchEvent( changeEvent );" << std::endl ;
            out << "" << std::endl ;
            out << "        this.update();" << std::endl ;
            out << "" << std::endl ;
            out << "    };" << std::endl ;
            out << "" << std::endl ;
            out << "    this.getPolarAngle = function () {" << std::endl ;
            out << "" << std::endl ;
            out << "        return phi;" << std::endl ;
            out << "" << std::endl ;
            out << "    };" << std::endl ;
            out << "" << std::endl ;
            out << "    this.getAzimuthalAngle = function () {" << std::endl ;
            out << "" << std::endl ;
            out << "        return theta" << std::endl ;
            out << "" << std::endl ;
            out << "    };" << std::endl ;
            out << "" << std::endl ;
            out << "    function getAutoRotationAngle() {" << std::endl ;
            out << "" << std::endl ;
            out << "        return 2 * Math.PI / 60 / 60 * scope.autoRotateSpeed;"
                << std::endl ;
            out << "" << std::endl ;
            out << "    }" << std::endl ;
            out << "" << std::endl ;
            out << "    function getZoomScale() {" << std::endl ;
            out << "" << std::endl ;
            out << "        return Math.pow( 0.95, scope.zoomSpeed );" << std::endl ;
            out << "" << std::endl ;
            out << "    }" << std::endl ;
            out << "" << std::endl ;
            out << "    function onMouseDown( event ) {" << std::endl ;
            out << "" << std::endl ;
            out << "        if ( scope.enabled === false ) return;" << std::endl ;
            out << "        event.preventDefault();" << std::endl ;
            out << "" << std::endl ;
            out << "        if ( event.button === scope.mouseButtons.ORBIT ) {"
                << std::endl ;
            out << "            if ( scope.noRotate === true ) return;"
                << std::endl ;
            out << "" << std::endl ;
            out << "            state = STATE.ROTATE;" << std::endl ;
            out << "" << std::endl ;
            out << "            rotateStart.set( event.clientX, event.clientY );"
                << std::endl ;
            out << "" << std::endl ;
            out << "        } else if ( event.button === scope.mouseButtons.ZOOM ) {"
                << std::endl ;
            out << "            if ( scope.noZoom === true ) return;" << std::endl ;
            out << "" << std::endl ;
            out << "            state = STATE.DOLLY;" << std::endl ;
            out << "" << std::endl ;
            out << "            dollyStart.set( event.clientX, event.clientY );"
                << std::endl ;
            out << "" << std::endl ;
            out << "        } else if ( event.button === scope.mouseButtons.PAN ) {"
                << std::endl ;
            out << "            if ( scope.noPan === true ) return;" << std::endl ;
            out << "" << std::endl ;
            out << "            state = STATE.PAN;" << std::endl ;
            out << "" << std::endl ;
            out << "            panStart.set( event.clientX, event.clientY );"
                << std::endl ;
            out << "" << std::endl ;
            out << "        }" << std::endl ;
            out << "" << std::endl ;
            out << "        if ( state !== STATE.NONE ) {" << std::endl ;
            out
                << "            document.addEventListener( 'mousemove', onMouseMove, false );"
                << std::endl ;
            out
                << "            document.addEventListener( 'mouseup', onMouseUp, false );"
                << std::endl ;
            out << "            scope.dispatchEvent( startEvent );" << std::endl ;
            out << "        }" << std::endl ;
            out << "" << std::endl ;
            out << "    }" << std::endl ;
            out << "" << std::endl ;
            out << "    function onMouseMove( event ) {" << std::endl ;
            out << "" << std::endl ;
            out << "        if ( scope.enabled === false ) return;" << std::endl ;
            out << "" << std::endl ;
            out << "        event.preventDefault();" << std::endl ;
            out << "" << std::endl ;
            out
                << "        var element = scope.domElement === document ? scope.domElement.body : scope.domElement;"
                << std::endl ;
            out << "" << std::endl ;
            out << "        if ( state === STATE.ROTATE ) {" << std::endl ;
            out << "" << std::endl ;
            out << "            if ( scope.noRotate === true ) return;"
                << std::endl ;
            out << "" << std::endl ;
            out << "            rotateEnd.set( event.clientX, event.clientY );"
                << std::endl ;
            out << "            rotateDelta.subVectors( rotateEnd, rotateStart );"
                << std::endl ;
            out << "" << std::endl ;
            out
                << "            // rotating across whole screen goes 360 degrees around"
                << std::endl ;
            out
                << "            scope.rotateLeft( 2 * Math.PI * rotateDelta.x / element.clientWidth * scope.rotateSpeed );"
                << std::endl ;
            out << "" << std::endl ;
            out
                << "            // rotating up and down along whole screen attempts to go 360, but limited to 180"
                << std::endl ;
            out
                << "            scope.rotateUp( 2 * Math.PI * rotateDelta.y / element.clientHeight * scope.rotateSpeed );"
                << std::endl ;
            out << "" << std::endl ;
            out << "            rotateStart.copy( rotateEnd );" << std::endl ;
            out << "" << std::endl ;
            out << "        } else if ( state === STATE.DOLLY ) {" << std::endl ;
            out << "" << std::endl ;
            out << "            if ( scope.noZoom === true ) return;" << std::endl ;
            out << "" << std::endl ;
            out << "            dollyEnd.set( event.clientX, event.clientY );"
                << std::endl ;
            out << "            dollyDelta.subVectors( dollyEnd, dollyStart );"
                << std::endl ;
            out << "" << std::endl ;
            out << "            if ( dollyDelta.y > 0 ) {" << std::endl ;
            out << "" << std::endl ;
            out << "                scope.dollyIn();" << std::endl ;
            out << "" << std::endl ;
            out << "            } else if ( dollyDelta.y < 0 ) {" << std::endl ;
            out << "" << std::endl ;
            out << "                scope.dollyOut();" << std::endl ;
            out << "" << std::endl ;
            out << "            }" << std::endl ;
            out << "" << std::endl ;
            out << "            dollyStart.copy( dollyEnd );" << std::endl ;
            out << "" << std::endl ;
            out << "        } else if ( state === STATE.PAN ) {" << std::endl ;
            out << "" << std::endl ;
            out << "            if ( scope.noPan === true ) return;" << std::endl ;
            out << "" << std::endl ;
            out << "            panEnd.set( event.clientX, event.clientY );"
                << std::endl ;
            out << "            panDelta.subVectors( panEnd, panStart );"
                << std::endl ;
            out << "" << std::endl ;
            out << "            scope.pan( panDelta.x, panDelta.y );" << std::endl ;
            out << "" << std::endl ;
            out << "            panStart.copy( panEnd );" << std::endl ;
            out << "" << std::endl ;
            out << "        }" << std::endl ;
            out << "" << std::endl ;
            out << "        if ( state !== STATE.NONE ) scope.update();"
                << std::endl ;
            out << "" << std::endl ;
            out << "    }" << std::endl ;
            out << "" << std::endl ;
            out << "    function onMouseUp( /* event */ ) {" << std::endl ;
            out << "" << std::endl ;
            out << "        if ( scope.enabled === false ) return;" << std::endl ;
            out << "" << std::endl ;
            out
                << "        document.removeEventListener( 'mousemove', onMouseMove, false );"
                << std::endl ;
            out
                << "        document.removeEventListener( 'mouseup', onMouseUp, false );"
                << std::endl ;
            out << "        scope.dispatchEvent( endEvent );" << std::endl ;
            out << "        state = STATE.NONE;" << std::endl ;
            out << "" << std::endl ;
            out << "    }" << std::endl ;
            out << "" << std::endl ;
            out << "    function onMouseWheel( event ) {" << std::endl ;
            out << "" << std::endl ;
            out
                << "        if ( scope.enabled === false || scope.noZoom === true || state !== STATE.NONE ) return;"
                << std::endl ;
            out << "" << std::endl ;
            out << "        event.preventDefault();" << std::endl ;
            out << "        event.stopPropagation();" << std::endl ;
            out << "" << std::endl ;
            out << "        var delta = 0;" << std::endl ;
            out << "" << std::endl ;
            out
                << "        if ( event.wheelDelta !== undefined ) { // WebKit / Opera / Explorer 9"
                << std::endl ;
            out << "" << std::endl ;
            out << "            delta = event.wheelDelta;" << std::endl ;
            out << "" << std::endl ;
            out << "        } else if ( event.detail !== undefined ) { // Firefox"
                << std::endl ;
            out << "" << std::endl ;
            out << "            delta = - event.detail;" << std::endl ;
            out << "" << std::endl ;
            out << "        }" << std::endl ;
            out << "" << std::endl ;
            out << "        if ( delta > 0 ) {" << std::endl ;
            out << "" << std::endl ;
            out << "            scope.dollyOut();" << std::endl ;
            out << "" << std::endl ;
            out << "        } else if ( delta < 0 ) {" << std::endl ;
            out << "" << std::endl ;
            out << "            scope.dollyIn();" << std::endl ;
            out << "" << std::endl ;
            out << "        }" << std::endl ;
            out << "" << std::endl ;
            out << "        scope.update();" << std::endl ;
            out << "        scope.dispatchEvent( startEvent );" << std::endl ;
            out << "        scope.dispatchEvent( endEvent );" << std::endl ;
            out << "" << std::endl ;
            out << "    }" << std::endl ;
            out << "" << std::endl ;
            out << "    function onKeyDown( event ) {" << std::endl ;
            out << "" << std::endl ;
            out
                << "        if ( scope.enabled === false || scope.noKeys === true || scope.noPan === true ) return;"
                << std::endl ;
            out << "" << std::endl ;
            out << "        switch ( event.keyCode ) {" << std::endl ;
            out << "" << std::endl ;
            out << "            case scope.keys.UP:" << std::endl ;
            out << "                scope.pan( 0, scope.keyPanSpeed );"
                << std::endl ;
            out << "                scope.update();" << std::endl ;
            out << "                break;" << std::endl ;
            out << "" << std::endl ;
            out << "            case scope.keys.BOTTOM:" << std::endl ;
            out << "                scope.pan( 0, - scope.keyPanSpeed );"
                << std::endl ;
            out << "                scope.update();" << std::endl ;
            out << "                break;" << std::endl ;
            out << "" << std::endl ;
            out << "            case scope.keys.LEFT:" << std::endl ;
            out << "                scope.pan( scope.keyPanSpeed, 0 );"
                << std::endl ;
            out << "                scope.update();" << std::endl ;
            out << "                break;" << std::endl ;
            out << "" << std::endl ;
            out << "            case scope.keys.RIGHT:" << std::endl ;
            out << "                scope.pan( - scope.keyPanSpeed, 0 );"
                << std::endl ;
            out << "                scope.update();" << std::endl ;
            out << "                break;" << std::endl ;
            out << "" << std::endl ;
            out << "        }" << std::endl ;
            out << "" << std::endl ;
            out << "    }" << std::endl ;
            out << "" << std::endl ;
            out << "    function touchstart( event ) {" << std::endl ;
            out << "" << std::endl ;
            out << "        if ( scope.enabled === false ) return;" << std::endl ;
            out << "" << std::endl ;
            out << "        switch ( event.touches.length ) {" << std::endl ;
            out << "" << std::endl ;
            out << "            case 1: // one-fingered touch: rotate" << std::endl ;
            out << "" << std::endl ;
            out << "                if ( scope.noRotate === true ) return;"
                << std::endl ;
            out << "" << std::endl ;
            out << "                state = STATE.TOUCH_ROTATE;" << std::endl ;
            out << "" << std::endl ;
            out
                << "                rotateStart.set( event.touches[ 0 ].pageX, event.touches[ 0 ].pageY );"
                << std::endl ;
            out << "                break;" << std::endl ;
            out << "" << std::endl ;
            out << "            case 2: // two-fingered touch: dolly" << std::endl ;
            out << "" << std::endl ;
            out << "                if ( scope.noZoom === true ) return;"
                << std::endl ;
            out << "" << std::endl ;
            out << "                state = STATE.TOUCH_DOLLY;" << std::endl ;
            out << "" << std::endl ;
            out
                << "                var dx = event.touches[ 0 ].pageX - event.touches[ 1 ].pageX;"
                << std::endl ;
            out
                << "                var dy = event.touches[ 0 ].pageY - event.touches[ 1 ].pageY;"
                << std::endl ;
            out << "                var distance = Math.sqrt( dx * dx + dy * dy );"
                << std::endl ;
            out << "                dollyStart.set( 0, distance );" << std::endl ;
            out << "                break;" << std::endl ;
            out << "" << std::endl ;
            out << "            case 3: // three-fingered touch: pan" << std::endl ;
            out << "" << std::endl ;
            out << "                if ( scope.noPan === true ) return;"
                << std::endl ;
            out << "" << std::endl ;
            out << "                state = STATE.TOUCH_PAN;" << std::endl ;
            out << "" << std::endl ;
            out
                << "                panStart.set( event.touches[ 0 ].pageX, event.touches[ 0 ].pageY );"
                << std::endl ;
            out << "                break;" << std::endl ;
            out << "" << std::endl ;
            out << "            default:" << std::endl ;
            out << "" << std::endl ;
            out << "                state = STATE.NONE;" << std::endl ;
            out << "" << std::endl ;
            out << "        }" << std::endl ;
            out << "" << std::endl ;
            out
                << "        if ( state !== STATE.NONE ) scope.dispatchEvent( startEvent );"
                << std::endl ;
            out << "" << std::endl ;
            out << "    }" << std::endl ;
            out << "" << std::endl ;
            out << "    function touchmove( event ) {" << std::endl ;
            out << "" << std::endl ;
            out << "        if ( scope.enabled === false ) return;" << std::endl ;
            out << "" << std::endl ;
            out << "        event.preventDefault();" << std::endl ;
            out << "        event.stopPropagation();" << std::endl ;
            out << "" << std::endl ;
            out
                << "        var element = scope.domElement === document ? scope.domElement.body : scope.domElement;"
                << std::endl ;
            out << "" << std::endl ;
            out << "        switch ( event.touches.length ) {" << std::endl ;
            out << "" << std::endl ;
            out << "            case 1: // one-fingered touch: rotate" << std::endl ;
            out << "" << std::endl ;
            out << "                if ( scope.noRotate === true ) return;"
                << std::endl ;
            out << "                if ( state !== STATE.TOUCH_ROTATE ) return;"
                << std::endl ;
            out << "" << std::endl ;
            out
                << "                rotateEnd.set( event.touches[ 0 ].pageX, event.touches[ 0 ].pageY );"
                << std::endl ;
            out
                << "                rotateDelta.subVectors( rotateEnd, rotateStart );"
                << std::endl ;
            out << "" << std::endl ;
            out
                << "                // rotating across whole screen goes 360 degrees around"
                << std::endl ;
            out
                << "                scope.rotateLeft( 2 * Math.PI * rotateDelta.x / element.clientWidth * scope.rotateSpeed );"
                << std::endl ;
            out
                << "                // rotating up and down along whole screen attempts to go 360, but limited to 180"
                << std::endl ;
            out
                << "                scope.rotateUp( 2 * Math.PI * rotateDelta.y / element.clientHeight * scope.rotateSpeed );"
                << std::endl ;
            out << "" << std::endl ;
            out << "                rotateStart.copy( rotateEnd );" << std::endl ;
            out << "" << std::endl ;
            out << "                scope.update();" << std::endl ;
            out << "                break;" << std::endl ;
            out << "" << std::endl ;
            out << "            case 2: // two-fingered touch: dolly" << std::endl ;
            out << "" << std::endl ;
            out << "                if ( scope.noZoom === true ) return;"
                << std::endl ;
            out << "                if ( state !== STATE.TOUCH_DOLLY ) return;"
                << std::endl ;
            out << "" << std::endl ;
            out
                << "                var dx = event.touches[ 0 ].pageX - event.touches[ 1 ].pageX;"
                << std::endl ;
            out
                << "                var dy = event.touches[ 0 ].pageY - event.touches[ 1 ].pageY;"
                << std::endl ;
            out << "                var distance = Math.sqrt( dx * dx + dy * dy );"
                << std::endl ;
            out << "" << std::endl ;
            out << "                dollyEnd.set( 0, distance );" << std::endl ;
            out << "                dollyDelta.subVectors( dollyEnd, dollyStart );"
                << std::endl ;
            out << "" << std::endl ;
            out << "                if ( dollyDelta.y > 0 ) {" << std::endl ;
            out << "" << std::endl ;
            out << "                    scope.dollyOut();" << std::endl ;
            out << "" << std::endl ;
            out << "                } else if ( dollyDelta.y < 0 ) {" << std::endl ;
            out << "" << std::endl ;
            out << "                    scope.dollyIn();" << std::endl ;
            out << "" << std::endl ;
            out << "                }" << std::endl ;
            out << "" << std::endl ;
            out << "                dollyStart.copy( dollyEnd );" << std::endl ;
            out << "" << std::endl ;
            out << "                scope.update();" << std::endl ;
            out << "                break;" << std::endl ;
            out << "" << std::endl ;
            out << "            case 3: // three-fingered touch: pan" << std::endl ;
            out << "" << std::endl ;
            out << "                if ( scope.noPan === true ) return;"
                << std::endl ;
            out << "                if ( state !== STATE.TOUCH_PAN ) return;"
                << std::endl ;
            out << "" << std::endl ;
            out
                << "                panEnd.set( event.touches[ 0 ].pageX, event.touches[ 0 ].pageY );"
                << std::endl ;
            out << "                panDelta.subVectors( panEnd, panStart );"
                << std::endl ;
            out << "" << std::endl ;
            out << "                scope.pan( panDelta.x, panDelta.y );"
                << std::endl ;
            out << "" << std::endl ;
            out << "                panStart.copy( panEnd );" << std::endl ;
            out << "" << std::endl ;
            out << "                scope.update();" << std::endl ;
            out << "                break;" << std::endl ;
            out << "" << std::endl ;
            out << "            default:" << std::endl ;
            out << "" << std::endl ;
            out << "                state = STATE.NONE;" << std::endl ;
            out << "" << std::endl ;
            out << "        }" << std::endl ;
            out << "" << std::endl ;
            out << "    }" << std::endl ;
            out << "" << std::endl ;
            out << "    function touchend( /* event */ ) {" << std::endl ;
            out << "" << std::endl ;
            out << "        if ( scope.enabled === false ) return;" << std::endl ;
            out << "" << std::endl ;
            out << "        scope.dispatchEvent( endEvent );" << std::endl ;
            out << "        state = STATE.NONE;" << std::endl ;
            out << "" << std::endl ;
            out << "    }" << std::endl ;
            out << "" << std::endl ;
            out
                << "    this.domElement.addEventListener( 'contextmenu', function ( event ) { event.preventDefault(); }, false );"
                << std::endl ;
            out
                << "    this.domElement.addEventListener( 'mousedown', onMouseDown, false );"
                << std::endl ;
            out
                << "    this.domElement.addEventListener( 'mousewheel', onMouseWheel, false );"
                << std::endl ;
            out
                << "    this.domElement.addEventListener( 'DOMMouseScroll', onMouseWheel, false ); // firefox"
                << std::endl ;
            out << "" << std::endl ;
            out
                << "    this.domElement.addEventListener( 'touchstart', touchstart, false );"
                << std::endl ;
            out
                << "    this.domElement.addEventListener( 'touchend', touchend, false );"
                << std::endl ;
            out
                << "    this.domElement.addEventListener( 'touchmove', touchmove, false );"
                << std::endl ;
            out << "" << std::endl ;
            out << "    window.addEventListener( 'keydown', onKeyDown, false );"
                << std::endl ;
            out << "" << std::endl ;
            out << "    // force an update at start" << std::endl ;
            out << "    this.update();" << std::endl ;
            out << "" << std::endl ;
            out << "};" << std::endl ;
            out << "" << std::endl ;
            out
                << "THREE.OrbitControls.prototype = Object.create( THREE.EventDispatcher.prototype );"
                << std::endl ;
            out << "THREE.OrbitControls.prototype.constructor = THREE.OrbitControls;"
                << std::endl ;
        }

        void print_script01( std::ofstream& out )
        {
            out << "function init() {" << std::endl ;
            out << "    scene = new THREE.Scene();" << std::endl ;
            out << "    renderer = new THREE.WebGLRenderer({" << std::endl ;
            out << "        alpha: false" << std::endl ;
            out << "    });" << std::endl ;
            out << "    renderer.setClearColor(0xffffff, 1.0);" << std::endl ;
            out << "    renderer.setSize(window.innerWidth, window.innerHeight);"
                << std::endl ;
            out << "" << std::endl ;
            out << "    document.body.appendChild(renderer.domElement);"
                << std::endl ;
            out
                << "    camera = new THREE.PerspectiveCamera(40, window.innerWidth / window.innerHeight, 0.1, 100000);"
                << std::endl ;
            out << "    initControl();" << std::endl ;
            out << "    initMaterials();" << std::endl ;
            out << "    initLights();" << std::endl ;
            out << "    loadObjects();" << std::endl ;
            out << "    setCameraPlace();" << std::endl ;
            out << "    scene.add(camera)" << std::endl ;
            out << "};" << std::endl ;
            out << "" << std::endl ;
            out << "var render = function render() {" << std::endl ;
            out << "    requestAnimationFrame(render);" << std::endl ;
            out
                << "    light.position.set(camera.position.x, camera.position.y, camera.position.z);"
                << std::endl ;
            out << "    renderer.render(scene, camera) ;" << std::endl ;
            out << "    controls.update();" << std::endl ;
            out << "};" << std::endl ;
            out << "" << std::endl ;
            out << "function setCameraPlace() {" << std::endl ;
            out << "    if (meshes.length == 0) {" << std::endl ;
            out << "        return" << std::endl ;
            out << "    }" << std::endl ;
            out << "    meshes[0].geometry.computeBoundingBox();" << std::endl ;
            out << "    minX = meshes[0].geometry.boundingBox.min.x;" << std::endl ;
            out << "    maxX = meshes[0].geometry.boundingBox.max.x;" << std::endl ;
            out << "    minY = meshes[0].geometry.boundingBox.min.y;" << std::endl ;
            out << "    maxY = meshes[0].geometry.boundingBox.max.y;" << std::endl ;
            out << "    minZ = meshes[0].geometry.boundingBox.min.z;" << std::endl ;
            out << "    maxZ = meshes[0].geometry.boundingBox.max.z;" << std::endl ;
            out << "    for (obj = 0; obj < meshes.length; ++obj) {" << std::endl ;
            out << "        if (meshes[obj].visible) {" << std::endl ;
            out << "            mesh = meshes[obj];" << std::endl ;
            out << "            mesh.geometry.computeBoundingBox();" << std::endl ;
            out << "            box3 = mesh.geometry.boundingBox;" << std::endl ;
            out << "            minX = Math.min(minX, box3.min.x);" << std::endl ;
            out << "            minY = Math.min(minY, box3.min.y);" << std::endl ;
            out << "            minZ = Math.min(minZ, box3.min.z);" << std::endl ;
            out << "            maxX = Math.max(maxX, box3.max.x);" << std::endl ;
            out << "            maxY = Math.max(maxY, box3.max.y);" << std::endl ;
            out << "            maxZ = Math.max(maxZ, box3.max.z)" << std::endl ;
            out << "        }" << std::endl ;
            out << "    }" << std::endl ;
            out << "" << std::endl ;
            out
                << "    center = new THREE.Vector3((minX + maxX) * 0.5, (minY + maxY) * 0.5, (minZ + maxZ) * 0.5);"
                << std::endl ;
            out
                << "    distance = Math.sqrt((maxX - minX) * (maxX - minX) + (maxY - minY) * (maxY - minY) + (maxZ - minZ) * (maxZ - minZ));"
                << std::endl ;
            out << "    for (obj = 0; obj < meshes.length; ++obj) {" << std::endl ;
            out << "        if (meshes[obj].visible) {" << std::endl ;
            out << "            meshes[obj].translateX(-center.x);" << std::endl ;
            out << "            meshes[obj].translateY(-center.y);" << std::endl ;
            out << "            meshes[obj].translateZ(-center.z);" << std::endl ;
            out << "        }" << std::endl ;
            out << "    }" << std::endl ;
            out
                << "    camera.position.set(distance * 0.75, distance * 0.75, -distance * 0.75);"
                << std::endl ;
            out << "    camera.up = new THREE.Vector3(0, 0.0, -1.0)" << std::endl ;
            out << "};" << std::endl ;
            out << "" << std::endl ;
            out << "function initControl() {" << std::endl ;
            out
                << "    controls = new THREE.TrackballControls(camera, renderer.domElement);"
                << std::endl ;
            out << "    controls.rotateSpeed *= 2.0;" << std::endl ;
            out << "    controls.zoomSpeed *= 2.0;" << std::endl ;
            out << "    controls.panSpeed *= 2.0" << std::endl ;
            out << "};" << std::endl ;
            out << "" << std::endl ;
            out << "function initMaterials() {" << std::endl ;
            out << "    material = new THREE.MeshLambertMaterial({" << std::endl ;
            out << "        color: 0xfffff," << std::endl ;
            out << "        shading: THREE.SmoothShading," << std::endl ;
            out << "        side: THREE.DoubleSide" << std::endl ;
            out << "    })" << std::endl ;
            out << "};" << std::endl ;
            out << "" << std::endl ;
            out << "function initLights() {" << std::endl ;
            out << "    ambientlight = new THREE.AmbientLight(0x404040);"
                << std::endl ;
            out << "    scene.add(ambientlight);" << std::endl ;
            out << "    light = new THREE.PointLight(0xfffaff, 0.8, 0);"
                << std::endl ;
            out
                << "    light.position.set(camera.position.x, camera.position.y, camera.position.z);"
                << std::endl ;
            out << "    scene.add(light)" << std::endl ;
            out << "};" << std::endl ;
            out << "" << std::endl ;
            out << "function loadTSurf(numbers) {" << std::endl ;
            out << "    number_of_points = numbers[0];" << std::endl ;
            out << "    number_of_triangles = numbers[1];" << std::endl ;
            out << "    var geometry = new THREE.Geometry();" << std::endl ;
            out << "    for (var p = 0; p < number_of_points; p++) {" << std::endl ;
            out
                << "        var v = new THREE.Vector3(numbers[2 + p * 3 + 0], numbers[2 + p * 3 + 1], numbers[2 + p * 3 + 2]);"
                << std::endl ;
            out << "        geometry.vertices.push(v)" << std::endl ;
            out << "    }" << std::endl ;
            out << "    var offset = 3 * number_of_points + 2;" << std::endl ;
            out << "    for (var t = 0; t < number_of_triangles; t++) {"
                << std::endl ;
            out
                << "        var f = new THREE.Face3(numbers[offset + t * 3 + 0] , numbers[offset + t * 3 + 1] , numbers[offset + t * 3 + 2] );"
                << std::endl ;
            out << "        geometry.faces.push(f);" << std::endl ;
            out << "    }" << std::endl ;
            out << "" << std::endl ;
            out << "    geometry.computeFaceNormals();" << std::endl ;
            out << "    mat = new THREE.MeshLambertMaterial(material);"
                << std::endl ;
            out << "    var surface = new THREE.Mesh(geometry, mat);" << std::endl ;
            out << "    meshes.push(surface);" << std::endl ;
            out << "" << std::endl ;
            out << "};" << std::endl ;
            out << "" << std::endl ;
            out << "function loadPLine(numbers){" << std::endl ;
            out << "" << std::endl ;
            out << "    var type = THREE.LineStrip ; //THREE.LinePieces"
                << std::endl ;
            out << "    var geometry = new THREE.Geometry();" << std::endl ;
            out << "    for (var p = 0; p < numbers.length / 3 ; p++) {"
                << std::endl ;
            out
                << "        var point = new THREE.Vector3(numbers[3*p+0],numbers[3*p+1],numbers[3*p+2]);"
                << std::endl ;
            out << "        geometry.vertices.push(point);" << std::endl ;
            out << "    }" << std::endl ;
            out
                << "    var line = new THREE.Line( geometry, new THREE.LineBasicMaterial( { color: Math.random() * 0xffffff , linewidth:5 }  ), type );"
                << std::endl ;
            out << "     meshes.push(line);" << std::endl ;
            out << "}" << std::endl ;
            out << "" << std::endl ;
            out << "function loadTSolid(numbers){" << std::endl ;
            out << "    loadTSurf(numbers);" << std::endl ;
            out << "}" << std::endl ;
            out << "" << std::endl ;
            out << "function loadVSet(numbers){" << std::endl ;
            out << "    var geometry = new THREE.Geometry();" << std::endl ;
            out << "    for (var p = 0; p < numbers.length / 3 ; p++) {"
                << std::endl ;
            out
                << "        var point = new THREE.Vector3(numbers[3*p+0],numbers[3*p+1],numbers[3*p+2]);"
                << std::endl ;
            out << "        geometry.vertices.push(point);" << std::endl ;
            out << "    }" << std::endl ;
            out
                << "    var pointCloud = new THREE.PointCloud( geometry, new THREE.PointCloudMaterial( { color: Math.random() * 0xffffff  } ) );"
                << std::endl ;
            out << "     meshes.push(pointCloud);" << std::endl ;
            out << "}" << std::endl ;
            out << "" << std::endl ;
            out << "" << std::endl ;
            out << "function addMeshes(){" << std::endl ;
            out << "            for (m = 0; m < meshes.length; ++m) {" << std::endl ;
            out << "                scene.add(meshes[m]);" << std::endl ;
            out << "            }" << std::endl ;
            out << "}" << std::endl ;
        }

        void print_three_min( std::ofstream& out )
        {
            out << "// threejs.org/license" << std::endl ;
            out
                << "'use strict';var THREE={REVISION:\"71\"};\"object\"===typeof module&&(module.exports=THREE);void 0===Math.sign&&(Math.sign=function(a){return 0>a?-1:0<a?1:+a});THREE.log=function(){console.log.apply(console,arguments)};THREE.warn=function(){console.warn.apply(console,arguments)};THREE.error=function(){console.error.apply(console,arguments)};THREE.MOUSE={LEFT:0,MIDDLE:1,RIGHT:2};THREE.CullFaceNone=0;THREE.CullFaceBack=1;THREE.CullFaceFront=2;THREE.CullFaceFrontBack=3;THREE.FrontFaceDirectionCW=0;"
                << std::endl ;
            out
                << "THREE.FrontFaceDirectionCCW=1;THREE.BasicShadowMap=0;THREE.PCFShadowMap=1;THREE.PCFSoftShadowMap=2;THREE.FrontSide=0;THREE.BackSide=1;THREE.DoubleSide=2;THREE.NoShading=0;THREE.FlatShading=1;THREE.SmoothShading=2;THREE.NoColors=0;THREE.FaceColors=1;THREE.VertexColors=2;THREE.NoBlending=0;THREE.NormalBlending=1;THREE.AdditiveBlending=2;THREE.SubtractiveBlending=3;THREE.MultiplyBlending=4;THREE.CustomBlending=5;THREE.AddEquation=100;THREE.SubtractEquation=101;THREE.ReverseSubtractEquation=102;"
                << std::endl ;
            out
                << "THREE.MinEquation=103;THREE.MaxEquation=104;THREE.ZeroFactor=200;THREE.OneFactor=201;THREE.SrcColorFactor=202;THREE.OneMinusSrcColorFactor=203;THREE.SrcAlphaFactor=204;THREE.OneMinusSrcAlphaFactor=205;THREE.DstAlphaFactor=206;THREE.OneMinusDstAlphaFactor=207;THREE.DstColorFactor=208;THREE.OneMinusDstColorFactor=209;THREE.SrcAlphaSaturateFactor=210;THREE.MultiplyOperation=0;THREE.MixOperation=1;THREE.AddOperation=2;THREE.UVMapping=300;THREE.CubeReflectionMapping=301;THREE.CubeRefractionMapping=302;"
                << std::endl ;
            out
                << "THREE.EquirectangularReflectionMapping=303;THREE.EquirectangularRefractionMapping=304;THREE.SphericalReflectionMapping=305;THREE.RepeatWrapping=1E3;THREE.ClampToEdgeWrapping=1001;THREE.MirroredRepeatWrapping=1002;THREE.NearestFilter=1003;THREE.NearestMipMapNearestFilter=1004;THREE.NearestMipMapLinearFilter=1005;THREE.LinearFilter=1006;THREE.LinearMipMapNearestFilter=1007;THREE.LinearMipMapLinearFilter=1008;THREE.UnsignedByteType=1009;THREE.ByteType=1010;THREE.ShortType=1011;"
                << std::endl ;
            out
                << "THREE.UnsignedShortType=1012;THREE.IntType=1013;THREE.UnsignedIntType=1014;THREE.FloatType=1015;THREE.HalfFloatType=1025;THREE.UnsignedShort4444Type=1016;THREE.UnsignedShort5551Type=1017;THREE.UnsignedShort565Type=1018;THREE.AlphaFormat=1019;THREE.RGBFormat=1020;THREE.RGBAFormat=1021;THREE.LuminanceFormat=1022;THREE.LuminanceAlphaFormat=1023;THREE.RGBEFormat=THREE.RGBAFormat;THREE.RGB_S3TC_DXT1_Format=2001;THREE.RGBA_S3TC_DXT1_Format=2002;THREE.RGBA_S3TC_DXT3_Format=2003;"
                << std::endl ;
            out
                << "THREE.RGBA_S3TC_DXT5_Format=2004;THREE.RGB_PVRTC_4BPPV1_Format=2100;THREE.RGB_PVRTC_2BPPV1_Format=2101;THREE.RGBA_PVRTC_4BPPV1_Format=2102;THREE.RGBA_PVRTC_2BPPV1_Format=2103;"
                << std::endl ;
            out
                << "THREE.Projector=function(){THREE.error(\"THREE.Projector has been moved to /examples/js/renderers/Projector.js.\");this.projectVector=function(a,b){THREE.warn(\"THREE.Projector: .projectVector() is now vector.project().\");a.project(b)};this.unprojectVector=function(a,b){THREE.warn(\"THREE.Projector: .unprojectVector() is now vector.unproject().\");a.unproject(b)};this.pickingRay=function(a,b){THREE.error(\"THREE.Projector: .pickingRay() is now raycaster.setFromCamera().\")}};"
                << std::endl ;
            out
                << "THREE.CanvasRenderer=function(){THREE.error(\"THREE.CanvasRenderer has been moved to /examples/js/renderers/CanvasRenderer.js\");this.domElement=document.createElement(\"canvas\");this.clear=function(){};this.render=function(){};this.setClearColor=function(){};this.setSize=function(){}};THREE.Color=function(a){return 3===arguments.length?this.setRGB(arguments[0],arguments[1],arguments[2]):this.set(a)};"
                << std::endl ;
            out
                << "THREE.Color.prototype={constructor:THREE.Color,r:1,g:1,b:1,set:function(a){a instanceof THREE.Color?this.copy(a):\"number\"===typeof a?this.setHex(a):\"string\"===typeof a&&this.setStyle(a);return this},setHex:function(a){a=Math.floor(a);this.r=(a>>16&255)/255;this.g=(a>>8&255)/255;this.b=(a&255)/255;return this},setRGB:function(a,b,c){this.r=a;this.g=b;this.b=c;return this},setHSL:function(a,b,c){if(0===b)this.r=this.g=this.b=c;else{var d=function(a,b,c){0>c&&(c+=1);1<c&&(c-=1);return c<1/6?a+6*(b-a)*"
                << std::endl ;
            out
                << "c:.5>c?b:c<2/3?a+6*(b-a)*(2/3-c):a};b=.5>=c?c*(1+b):c+b-c*b;c=2*c-b;this.r=d(c,b,a+1/3);this.g=d(c,b,a);this.b=d(c,b,a-1/3)}return this},setStyle:function(a){if(/^rgb\\((\\d+), ?(\\d+), ?(\\d+)\\)$/i.test(a))return a=/^rgb\\((\\d+), ?(\\d+), ?(\\d+)\\)$/i.exec(a),this.r=Math.min(255,parseInt(a[1],10))/255,this.g=Math.min(255,parseInt(a[2],10))/255,this.b=Math.min(255,parseInt(a[3],10))/255,this;if(/^rgb\\((\\d+)\\%, ?(\\d+)\\%, ?(\\d+)\\%\\)$/i.test(a))return a=/^rgb\\((\\d+)\\%, ?(\\d+)\\%, ?(\\d+)\\%\\)$/i.exec(a),this.r="
                << std::endl ;
            out
                << "Math.min(100,parseInt(a[1],10))/100,this.g=Math.min(100,parseInt(a[2],10))/100,this.b=Math.min(100,parseInt(a[3],10))/100,this;if(/^\\#([0-9a-f]{6})$/i.test(a))return a=/^\\#([0-9a-f]{6})$/i.exec(a),this.setHex(parseInt(a[1],16)),this;if(/^\\#([0-9a-f])([0-9a-f])([0-9a-f])$/i.test(a))return a=/^\\#([0-9a-f])([0-9a-f])([0-9a-f])$/i.exec(a),this.setHex(parseInt(a[1]+a[1]+a[2]+a[2]+a[3]+a[3],16)),this;if(/^(\\w+)$/i.test(a))return this.setHex(THREE.ColorKeywords[a]),this},copy:function(a){this.r=a.r;this.g="
                << std::endl ;
            out
                << "a.g;this.b=a.b;return this},copyGammaToLinear:function(a,b){void 0===b&&(b=2);this.r=Math.pow(a.r,b);this.g=Math.pow(a.g,b);this.b=Math.pow(a.b,b);return this},copyLinearToGamma:function(a,b){void 0===b&&(b=2);var c=0<b?1/b:1;this.r=Math.pow(a.r,c);this.g=Math.pow(a.g,c);this.b=Math.pow(a.b,c);return this},convertGammaToLinear:function(){var a=this.r,b=this.g,c=this.b;this.r=a*a;this.g=b*b;this.b=c*c;return this},convertLinearToGamma:function(){this.r=Math.sqrt(this.r);this.g=Math.sqrt(this.g);this.b="
                << std::endl ;
            out
                << "Math.sqrt(this.b);return this},getHex:function(){return 255*this.r<<16^255*this.g<<8^255*this.b<<0},getHexString:function(){return(\"000000\"+this.getHex().toString(16)).slice(-6)},getHSL:function(a){a=a||{h:0,s:0,l:0};var b=this.r,c=this.g,d=this.b,e=Math.max(b,c,d),f=Math.min(b,c,d),g,h=(f+e)/2;if(f===e)f=g=0;else{var k=e-f,f=.5>=h?k/(e+f):k/(2-e-f);switch(e){case b:g=(c-d)/k+(c<d?6:0);break;case c:g=(d-b)/k+2;break;case d:g=(b-c)/k+4}g/=6}a.h=g;a.s=f;a.l=h;return a},getStyle:function(){return\"rgb(\"+"
                << std::endl ;
            out
                << "(255*this.r|0)+\",\"+(255*this.g|0)+\",\"+(255*this.b|0)+\")\"},offsetHSL:function(a,b,c){var d=this.getHSL();d.h+=a;d.s+=b;d.l+=c;this.setHSL(d.h,d.s,d.l);return this},add:function(a){this.r+=a.r;this.g+=a.g;this.b+=a.b;return this},addColors:function(a,b){this.r=a.r+b.r;this.g=a.g+b.g;this.b=a.b+b.b;return this},addScalar:function(a){this.r+=a;this.g+=a;this.b+=a;return this},multiply:function(a){this.r*=a.r;this.g*=a.g;this.b*=a.b;return this},multiplyScalar:function(a){this.r*=a;this.g*=a;this.b*=a;"
                << std::endl ;
            out
                << "return this},lerp:function(a,b){this.r+=(a.r-this.r)*b;this.g+=(a.g-this.g)*b;this.b+=(a.b-this.b)*b;return this},equals:function(a){return a.r===this.r&&a.g===this.g&&a.b===this.b},fromArray:function(a){this.r=a[0];this.g=a[1];this.b=a[2];return this},toArray:function(a,b){void 0===a&&(a=[]);void 0===b&&(b=0);a[b]=this.r;a[b+1]=this.g;a[b+2]=this.b;return a},clone:function(){return(new THREE.Color).setRGB(this.r,this.g,this.b)}};"
                << std::endl ;
            out
                << "THREE.ColorKeywords={aliceblue:15792383,antiquewhite:16444375,aqua:65535,aquamarine:8388564,azure:15794175,beige:16119260,bisque:16770244,black:0,blanchedalmond:16772045,blue:255,blueviolet:9055202,brown:10824234,burlywood:14596231,cadetblue:6266528,chartreuse:8388352,chocolate:13789470,coral:16744272,cornflowerblue:6591981,cornsilk:16775388,crimson:14423100,cyan:65535,darkblue:139,darkcyan:35723,darkgoldenrod:12092939,darkgray:11119017,darkgreen:25600,darkgrey:11119017,darkkhaki:12433259,darkmagenta:9109643,"
                << std::endl ;
            out
                << "darkolivegreen:5597999,darkorange:16747520,darkorchid:10040012,darkred:9109504,darksalmon:15308410,darkseagreen:9419919,darkslateblue:4734347,darkslategray:3100495,darkslategrey:3100495,darkturquoise:52945,darkviolet:9699539,deeppink:16716947,deepskyblue:49151,dimgray:6908265,dimgrey:6908265,dodgerblue:2003199,firebrick:11674146,floralwhite:16775920,forestgreen:2263842,fuchsia:16711935,gainsboro:14474460,ghostwhite:16316671,gold:16766720,goldenrod:14329120,gray:8421504,green:32768,greenyellow:11403055,"
                << std::endl ;
            out
                << "grey:8421504,honeydew:15794160,hotpink:16738740,indianred:13458524,indigo:4915330,ivory:16777200,khaki:15787660,lavender:15132410,lavenderblush:16773365,lawngreen:8190976,lemonchiffon:16775885,lightblue:11393254,lightcoral:15761536,lightcyan:14745599,lightgoldenrodyellow:16448210,lightgray:13882323,lightgreen:9498256,lightgrey:13882323,lightpink:16758465,lightsalmon:16752762,lightseagreen:2142890,lightskyblue:8900346,lightslategray:7833753,lightslategrey:7833753,lightsteelblue:11584734,lightyellow:16777184,"
                << std::endl ;
            out
                << "lime:65280,limegreen:3329330,linen:16445670,magenta:16711935,maroon:8388608,mediumaquamarine:6737322,mediumblue:205,mediumorchid:12211667,mediumpurple:9662683,mediumseagreen:3978097,mediumslateblue:8087790,mediumspringgreen:64154,mediumturquoise:4772300,mediumvioletred:13047173,midnightblue:1644912,mintcream:16121850,mistyrose:16770273,moccasin:16770229,navajowhite:16768685,navy:128,oldlace:16643558,olive:8421376,olivedrab:7048739,orange:16753920,orangered:16729344,orchid:14315734,palegoldenrod:15657130,"
                << std::endl ;
            out
                << "palegreen:10025880,paleturquoise:11529966,palevioletred:14381203,papayawhip:16773077,peachpuff:16767673,peru:13468991,pink:16761035,plum:14524637,powderblue:11591910,purple:8388736,red:16711680,rosybrown:12357519,royalblue:4286945,saddlebrown:9127187,salmon:16416882,sandybrown:16032864,seagreen:3050327,seashell:16774638,sienna:10506797,silver:12632256,skyblue:8900331,slateblue:6970061,slategray:7372944,slategrey:7372944,snow:16775930,springgreen:65407,steelblue:4620980,tan:13808780,teal:32896,thistle:14204888,"
                << std::endl ;
            out
                << "tomato:16737095,turquoise:4251856,violet:15631086,wheat:16113331,white:16777215,whitesmoke:16119285,yellow:16776960,yellowgreen:10145074};THREE.Quaternion=function(a,b,c,d){this._x=a||0;this._y=b||0;this._z=c||0;this._w=void 0!==d?d:1};"
                << std::endl ;
            out
                << "THREE.Quaternion.prototype={constructor:THREE.Quaternion,_x:0,_y:0,_z:0,_w:0,get x(){return this._x},set x(a){this._x=a;this.onChangeCallback()},get y(){return this._y},set y(a){this._y=a;this.onChangeCallback()},get z(){return this._z},set z(a){this._z=a;this.onChangeCallback()},get w(){return this._w},set w(a){this._w=a;this.onChangeCallback()},set:function(a,b,c,d){this._x=a;this._y=b;this._z=c;this._w=d;this.onChangeCallback();return this},copy:function(a){this._x=a.x;this._y=a.y;this._z=a.z;"
                << std::endl ;
            out
                << "this._w=a.w;this.onChangeCallback();return this},setFromEuler:function(a,b){if(!1===a instanceof THREE.Euler)throw Error(\"THREE.Quaternion: .setFromEuler() now expects a Euler rotation rather than a Vector3 and order.\");var c=Math.cos(a._x/2),d=Math.cos(a._y/2),e=Math.cos(a._z/2),f=Math.sin(a._x/2),g=Math.sin(a._y/2),h=Math.sin(a._z/2);\"XYZ\"===a.order?(this._x=f*d*e+c*g*h,this._y=c*g*e-f*d*h,this._z=c*d*h+f*g*e,this._w=c*d*e-f*g*h):\"YXZ\"===a.order?(this._x=f*d*e+c*g*h,this._y=c*g*e-f*d*h,this._z="
                << std::endl ;
            out
                << "c*d*h-f*g*e,this._w=c*d*e+f*g*h):\"ZXY\"===a.order?(this._x=f*d*e-c*g*h,this._y=c*g*e+f*d*h,this._z=c*d*h+f*g*e,this._w=c*d*e-f*g*h):\"ZYX\"===a.order?(this._x=f*d*e-c*g*h,this._y=c*g*e+f*d*h,this._z=c*d*h-f*g*e,this._w=c*d*e+f*g*h):\"YZX\"===a.order?(this._x=f*d*e+c*g*h,this._y=c*g*e+f*d*h,this._z=c*d*h-f*g*e,this._w=c*d*e-f*g*h):\"XZY\"===a.order&&(this._x=f*d*e-c*g*h,this._y=c*g*e-f*d*h,this._z=c*d*h+f*g*e,this._w=c*d*e+f*g*h);if(!1!==b)this.onChangeCallback();return this},setFromAxisAngle:function(a,"
                << std::endl ;
            out
                << "b){var c=b/2,d=Math.sin(c);this._x=a.x*d;this._y=a.y*d;this._z=a.z*d;this._w=Math.cos(c);this.onChangeCallback();return this},setFromRotationMatrix:function(a){var b=a.elements,c=b[0];a=b[4];var d=b[8],e=b[1],f=b[5],g=b[9],h=b[2],k=b[6],b=b[10],l=c+f+b;0<l?(c=.5/Math.sqrt(l+1),this._w=.25/c,this._x=(k-g)*c,this._y=(d-h)*c,this._z=(e-a)*c):c>f&&c>b?(c=2*Math.sqrt(1+c-f-b),this._w=(k-g)/c,this._x=.25*c,this._y=(a+e)/c,this._z=(d+h)/c):f>b?(c=2*Math.sqrt(1+f-c-b),this._w=(d-h)/c,this._x=(a+e)/c,this._y="
                << std::endl ;
            out
                << ".25*c,this._z=(g+k)/c):(c=2*Math.sqrt(1+b-c-f),this._w=(e-a)/c,this._x=(d+h)/c,this._y=(g+k)/c,this._z=.25*c);this.onChangeCallback();return this},setFromUnitVectors:function(){var a,b;return function(c,d){void 0===a&&(a=new THREE.Vector3);b=c.dot(d)+1;1E-6>b?(b=0,Math.abs(c.x)>Math.abs(c.z)?a.set(-c.y,c.x,0):a.set(0,-c.z,c.y)):a.crossVectors(c,d);this._x=a.x;this._y=a.y;this._z=a.z;this._w=b;this.normalize();return this}}(),inverse:function(){this.conjugate().normalize();return this},conjugate:function(){this._x*="
                << std::endl ;
            out
                << "-1;this._y*=-1;this._z*=-1;this.onChangeCallback();return this},dot:function(a){return this._x*a._x+this._y*a._y+this._z*a._z+this._w*a._w},lengthSq:function(){return this._x*this._x+this._y*this._y+this._z*this._z+this._w*this._w},length:function(){return Math.sqrt(this._x*this._x+this._y*this._y+this._z*this._z+this._w*this._w)},normalize:function(){var a=this.length();0===a?(this._z=this._y=this._x=0,this._w=1):(a=1/a,this._x*=a,this._y*=a,this._z*=a,this._w*=a);this.onChangeCallback();return this},"
                << std::endl ;
            out
                << "multiply:function(a,b){return void 0!==b?(THREE.warn(\"THREE.Quaternion: .multiply() now only accepts one argument. Use .multiplyQuaternions( a, b ) instead.\"),this.multiplyQuaternions(a,b)):this.multiplyQuaternions(this,a)},multiplyQuaternions:function(a,b){var c=a._x,d=a._y,e=a._z,f=a._w,g=b._x,h=b._y,k=b._z,l=b._w;this._x=c*l+f*g+d*k-e*h;this._y=d*l+f*h+e*g-c*k;this._z=e*l+f*k+c*h-d*g;this._w=f*l-c*g-d*h-e*k;this.onChangeCallback();return this},multiplyVector3:function(a){THREE.warn(\"THREE.Quaternion: .multiplyVector3() has been removed. Use is now vector.applyQuaternion( quaternion ) instead.\");"
                << std::endl ;
            out
                << "return a.applyQuaternion(this)},slerp:function(a,b){if(0===b)return this;if(1===b)return this.copy(a);var c=this._x,d=this._y,e=this._z,f=this._w,g=f*a._w+c*a._x+d*a._y+e*a._z;0>g?(this._w=-a._w,this._x=-a._x,this._y=-a._y,this._z=-a._z,g=-g):this.copy(a);if(1<=g)return this._w=f,this._x=c,this._y=d,this._z=e,this;var h=Math.acos(g),k=Math.sqrt(1-g*g);if(.001>Math.abs(k))return this._w=.5*(f+this._w),this._x=.5*(c+this._x),this._y=.5*(d+this._y),this._z=.5*(e+this._z),this;g=Math.sin((1-b)*h)/k;h="
                << std::endl ;
            out
                << "Math.sin(b*h)/k;this._w=f*g+this._w*h;this._x=c*g+this._x*h;this._y=d*g+this._y*h;this._z=e*g+this._z*h;this.onChangeCallback();return this},equals:function(a){return a._x===this._x&&a._y===this._y&&a._z===this._z&&a._w===this._w},fromArray:function(a,b){void 0===b&&(b=0);this._x=a[b];this._y=a[b+1];this._z=a[b+2];this._w=a[b+3];this.onChangeCallback();return this},toArray:function(a,b){void 0===a&&(a=[]);void 0===b&&(b=0);a[b]=this._x;a[b+1]=this._y;a[b+2]=this._z;a[b+3]=this._w;return a},onChange:function(a){this.onChangeCallback="
                << std::endl ;
            out
                << "a;return this},onChangeCallback:function(){},clone:function(){return new THREE.Quaternion(this._x,this._y,this._z,this._w)}};THREE.Quaternion.slerp=function(a,b,c,d){return c.copy(a).slerp(b,d)};THREE.Vector2=function(a,b){this.x=a||0;this.y=b||0};"
                << std::endl ;
            out
                << "THREE.Vector2.prototype={constructor:THREE.Vector2,set:function(a,b){this.x=a;this.y=b;return this},setX:function(a){this.x=a;return this},setY:function(a){this.y=a;return this},setComponent:function(a,b){switch(a){case 0:this.x=b;break;case 1:this.y=b;break;default:throw Error(\"index is out of range: \"+a);}},getComponent:function(a){switch(a){case 0:return this.x;case 1:return this.y;default:throw Error(\"index is out of range: \"+a);}},copy:function(a){this.x=a.x;this.y=a.y;return this},add:function(a,"
                << std::endl ;
            out
                << "b){if(void 0!==b)return THREE.warn(\"THREE.Vector2: .add() now only accepts one argument. Use .addVectors( a, b ) instead.\"),this.addVectors(a,b);this.x+=a.x;this.y+=a.y;return this},addScalar:function(a){this.x+=a;this.y+=a;return this},addVectors:function(a,b){this.x=a.x+b.x;this.y=a.y+b.y;return this},sub:function(a,b){if(void 0!==b)return THREE.warn(\"THREE.Vector2: .sub() now only accepts one argument. Use .subVectors( a, b ) instead.\"),this.subVectors(a,b);this.x-=a.x;this.y-=a.y;return this},"
                << std::endl ;
            out
                << "subScalar:function(a){this.x-=a;this.y-=a;return this},subVectors:function(a,b){this.x=a.x-b.x;this.y=a.y-b.y;return this},multiply:function(a){this.x*=a.x;this.y*=a.y;return this},multiplyScalar:function(a){this.x*=a;this.y*=a;return this},divide:function(a){this.x/=a.x;this.y/=a.y;return this},divideScalar:function(a){0!==a?(a=1/a,this.x*=a,this.y*=a):this.y=this.x=0;return this},min:function(a){this.x>a.x&&(this.x=a.x);this.y>a.y&&(this.y=a.y);return this},max:function(a){this.x<a.x&&(this.x=a.x);"
                << std::endl ;
            out
                << "this.y<a.y&&(this.y=a.y);return this},clamp:function(a,b){this.x<a.x?this.x=a.x:this.x>b.x&&(this.x=b.x);this.y<a.y?this.y=a.y:this.y>b.y&&(this.y=b.y);return this},clampScalar:function(){var a,b;return function(c,d){void 0===a&&(a=new THREE.Vector2,b=new THREE.Vector2);a.set(c,c);b.set(d,d);return this.clamp(a,b)}}(),floor:function(){this.x=Math.floor(this.x);this.y=Math.floor(this.y);return this},ceil:function(){this.x=Math.ceil(this.x);this.y=Math.ceil(this.y);return this},round:function(){this.x="
                << std::endl ;
            out
                << "Math.round(this.x);this.y=Math.round(this.y);return this},roundToZero:function(){this.x=0>this.x?Math.ceil(this.x):Math.floor(this.x);this.y=0>this.y?Math.ceil(this.y):Math.floor(this.y);return this},negate:function(){this.x=-this.x;this.y=-this.y;return this},dot:function(a){return this.x*a.x+this.y*a.y},lengthSq:function(){return this.x*this.x+this.y*this.y},length:function(){return Math.sqrt(this.x*this.x+this.y*this.y)},normalize:function(){return this.divideScalar(this.length())},distanceTo:function(a){return Math.sqrt(this.distanceToSquared(a))},"
                << std::endl ;
            out
                << "distanceToSquared:function(a){var b=this.x-a.x;a=this.y-a.y;return b*b+a*a},setLength:function(a){var b=this.length();0!==b&&a!==b&&this.multiplyScalar(a/b);return this},lerp:function(a,b){this.x+=(a.x-this.x)*b;this.y+=(a.y-this.y)*b;return this},lerpVectors:function(a,b,c){this.subVectors(b,a).multiplyScalar(c).add(a);return this},equals:function(a){return a.x===this.x&&a.y===this.y},fromArray:function(a,b){void 0===b&&(b=0);this.x=a[b];this.y=a[b+1];return this},toArray:function(a,b){void 0==="
                << std::endl ;
            out
                << "a&&(a=[]);void 0===b&&(b=0);a[b]=this.x;a[b+1]=this.y;return a},fromAttribute:function(a,b,c){void 0===c&&(c=0);b=b*a.itemSize+c;this.x=a.array[b];this.y=a.array[b+1];return this},clone:function(){return new THREE.Vector2(this.x,this.y)}};THREE.Vector3=function(a,b,c){this.x=a||0;this.y=b||0;this.z=c||0};"
                << std::endl ;
            out
                << "THREE.Vector3.prototype={constructor:THREE.Vector3,set:function(a,b,c){this.x=a;this.y=b;this.z=c;return this},setX:function(a){this.x=a;return this},setY:function(a){this.y=a;return this},setZ:function(a){this.z=a;return this},setComponent:function(a,b){switch(a){case 0:this.x=b;break;case 1:this.y=b;break;case 2:this.z=b;break;default:throw Error(\"index is out of range: \"+a);}},getComponent:function(a){switch(a){case 0:return this.x;case 1:return this.y;case 2:return this.z;default:throw Error(\"index is out of range: \"+"
                << std::endl ;
            out
                << "a);}},copy:function(a){this.x=a.x;this.y=a.y;this.z=a.z;return this},add:function(a,b){if(void 0!==b)return THREE.warn(\"THREE.Vector3: .add() now only accepts one argument. Use .addVectors( a, b ) instead.\"),this.addVectors(a,b);this.x+=a.x;this.y+=a.y;this.z+=a.z;return this},addScalar:function(a){this.x+=a;this.y+=a;this.z+=a;return this},addVectors:function(a,b){this.x=a.x+b.x;this.y=a.y+b.y;this.z=a.z+b.z;return this},sub:function(a,b){if(void 0!==b)return THREE.warn(\"THREE.Vector3: .sub() now only accepts one argument. Use .subVectors( a, b ) instead.\"),"
                << std::endl ;
            out
                << "this.subVectors(a,b);this.x-=a.x;this.y-=a.y;this.z-=a.z;return this},subScalar:function(a){this.x-=a;this.y-=a;this.z-=a;return this},subVectors:function(a,b){this.x=a.x-b.x;this.y=a.y-b.y;this.z=a.z-b.z;return this},multiply:function(a,b){if(void 0!==b)return THREE.warn(\"THREE.Vector3: .multiply() now only accepts one argument. Use .multiplyVectors( a, b ) instead.\"),this.multiplyVectors(a,b);this.x*=a.x;this.y*=a.y;this.z*=a.z;return this},multiplyScalar:function(a){this.x*=a;this.y*=a;this.z*="
                << std::endl ;
            out
                << "a;return this},multiplyVectors:function(a,b){this.x=a.x*b.x;this.y=a.y*b.y;this.z=a.z*b.z;return this},applyEuler:function(){var a;return function(b){!1===b instanceof THREE.Euler&&THREE.error(\"THREE.Vector3: .applyEuler() now expects a Euler rotation rather than a Vector3 and order.\");void 0===a&&(a=new THREE.Quaternion);this.applyQuaternion(a.setFromEuler(b));return this}}(),applyAxisAngle:function(){var a;return function(b,c){void 0===a&&(a=new THREE.Quaternion);this.applyQuaternion(a.setFromAxisAngle(b,"
                << std::endl ;
            out
                << "c));return this}}(),applyMatrix3:function(a){var b=this.x,c=this.y,d=this.z;a=a.elements;this.x=a[0]*b+a[3]*c+a[6]*d;this.y=a[1]*b+a[4]*c+a[7]*d;this.z=a[2]*b+a[5]*c+a[8]*d;return this},applyMatrix4:function(a){var b=this.x,c=this.y,d=this.z;a=a.elements;this.x=a[0]*b+a[4]*c+a[8]*d+a[12];this.y=a[1]*b+a[5]*c+a[9]*d+a[13];this.z=a[2]*b+a[6]*c+a[10]*d+a[14];return this},applyProjection:function(a){var b=this.x,c=this.y,d=this.z;a=a.elements;var e=1/(a[3]*b+a[7]*c+a[11]*d+a[15]);this.x=(a[0]*b+a[4]*"
                << std::endl ;
            out
                << "c+a[8]*d+a[12])*e;this.y=(a[1]*b+a[5]*c+a[9]*d+a[13])*e;this.z=(a[2]*b+a[6]*c+a[10]*d+a[14])*e;return this},applyQuaternion:function(a){var b=this.x,c=this.y,d=this.z,e=a.x,f=a.y,g=a.z;a=a.w;var h=a*b+f*d-g*c,k=a*c+g*b-e*d,l=a*d+e*c-f*b,b=-e*b-f*c-g*d;this.x=h*a+b*-e+k*-g-l*-f;this.y=k*a+b*-f+l*-e-h*-g;this.z=l*a+b*-g+h*-f-k*-e;return this},project:function(){var a;return function(b){void 0===a&&(a=new THREE.Matrix4);a.multiplyMatrices(b.projectionMatrix,a.getInverse(b.matrixWorld));return this.applyProjection(a)}}(),"
                << std::endl ;
            out
                << "unproject:function(){var a;return function(b){void 0===a&&(a=new THREE.Matrix4);a.multiplyMatrices(b.matrixWorld,a.getInverse(b.projectionMatrix));return this.applyProjection(a)}}(),transformDirection:function(a){var b=this.x,c=this.y,d=this.z;a=a.elements;this.x=a[0]*b+a[4]*c+a[8]*d;this.y=a[1]*b+a[5]*c+a[9]*d;this.z=a[2]*b+a[6]*c+a[10]*d;this.normalize();return this},divide:function(a){this.x/=a.x;this.y/=a.y;this.z/=a.z;return this},divideScalar:function(a){0!==a?(a=1/a,this.x*=a,this.y*=a,this.z*="
                << std::endl ;
            out
                << "a):this.z=this.y=this.x=0;return this},min:function(a){this.x>a.x&&(this.x=a.x);this.y>a.y&&(this.y=a.y);this.z>a.z&&(this.z=a.z);return this},max:function(a){this.x<a.x&&(this.x=a.x);this.y<a.y&&(this.y=a.y);this.z<a.z&&(this.z=a.z);return this},clamp:function(a,b){this.x<a.x?this.x=a.x:this.x>b.x&&(this.x=b.x);this.y<a.y?this.y=a.y:this.y>b.y&&(this.y=b.y);this.z<a.z?this.z=a.z:this.z>b.z&&(this.z=b.z);return this},clampScalar:function(){var a,b;return function(c,d){void 0===a&&(a=new THREE.Vector3,"
                << std::endl ;
            out
                << "b=new THREE.Vector3);a.set(c,c,c);b.set(d,d,d);return this.clamp(a,b)}}(),floor:function(){this.x=Math.floor(this.x);this.y=Math.floor(this.y);this.z=Math.floor(this.z);return this},ceil:function(){this.x=Math.ceil(this.x);this.y=Math.ceil(this.y);this.z=Math.ceil(this.z);return this},round:function(){this.x=Math.round(this.x);this.y=Math.round(this.y);this.z=Math.round(this.z);return this},roundToZero:function(){this.x=0>this.x?Math.ceil(this.x):Math.floor(this.x);this.y=0>this.y?Math.ceil(this.y):"
                << std::endl ;
            out
                << "Math.floor(this.y);this.z=0>this.z?Math.ceil(this.z):Math.floor(this.z);return this},negate:function(){this.x=-this.x;this.y=-this.y;this.z=-this.z;return this},dot:function(a){return this.x*a.x+this.y*a.y+this.z*a.z},lengthSq:function(){return this.x*this.x+this.y*this.y+this.z*this.z},length:function(){return Math.sqrt(this.x*this.x+this.y*this.y+this.z*this.z)},lengthManhattan:function(){return Math.abs(this.x)+Math.abs(this.y)+Math.abs(this.z)},normalize:function(){return this.divideScalar(this.length())},"
                << std::endl ;
            out
                << "setLength:function(a){var b=this.length();0!==b&&a!==b&&this.multiplyScalar(a/b);return this},lerp:function(a,b){this.x+=(a.x-this.x)*b;this.y+=(a.y-this.y)*b;this.z+=(a.z-this.z)*b;return this},lerpVectors:function(a,b,c){this.subVectors(b,a).multiplyScalar(c).add(a);return this},cross:function(a,b){if(void 0!==b)return THREE.warn(\"THREE.Vector3: .cross() now only accepts one argument. Use .crossVectors( a, b ) instead.\"),this.crossVectors(a,b);var c=this.x,d=this.y,e=this.z;this.x=d*a.z-e*a.y;this.y="
                << std::endl ;
            out
                << "e*a.x-c*a.z;this.z=c*a.y-d*a.x;return this},crossVectors:function(a,b){var c=a.x,d=a.y,e=a.z,f=b.x,g=b.y,h=b.z;this.x=d*h-e*g;this.y=e*f-c*h;this.z=c*g-d*f;return this},projectOnVector:function(){var a,b;return function(c){void 0===a&&(a=new THREE.Vector3);a.copy(c).normalize();b=this.dot(a);return this.copy(a).multiplyScalar(b)}}(),projectOnPlane:function(){var a;return function(b){void 0===a&&(a=new THREE.Vector3);a.copy(this).projectOnVector(b);return this.sub(a)}}(),reflect:function(){var a;return function(b){void 0==="
                << std::endl ;
            out
                << "a&&(a=new THREE.Vector3);return this.sub(a.copy(b).multiplyScalar(2*this.dot(b)))}}(),angleTo:function(a){a=this.dot(a)/(this.length()*a.length());return Math.acos(THREE.Math.clamp(a,-1,1))},distanceTo:function(a){return Math.sqrt(this.distanceToSquared(a))},distanceToSquared:function(a){var b=this.x-a.x,c=this.y-a.y;a=this.z-a.z;return b*b+c*c+a*a},setEulerFromRotationMatrix:function(a,b){THREE.error(\"THREE.Vector3: .setEulerFromRotationMatrix() has been removed. Use Euler.setFromRotationMatrix() instead.\")},"
                << std::endl ;
            out
                << "setEulerFromQuaternion:function(a,b){THREE.error(\"THREE.Vector3: .setEulerFromQuaternion() has been removed. Use Euler.setFromQuaternion() instead.\")},getPositionFromMatrix:function(a){THREE.warn(\"THREE.Vector3: .getPositionFromMatrix() has been renamed to .setFromMatrixPosition().\");return this.setFromMatrixPosition(a)},getScaleFromMatrix:function(a){THREE.warn(\"THREE.Vector3: .getScaleFromMatrix() has been renamed to .setFromMatrixScale().\");return this.setFromMatrixScale(a)},getColumnFromMatrix:function(a,"
                << std::endl ;
            out
                << "b){THREE.warn(\"THREE.Vector3: .getColumnFromMatrix() has been renamed to .setFromMatrixColumn().\");return this.setFromMatrixColumn(a,b)},setFromMatrixPosition:function(a){this.x=a.elements[12];this.y=a.elements[13];this.z=a.elements[14];return this},setFromMatrixScale:function(a){var b=this.set(a.elements[0],a.elements[1],a.elements[2]).length(),c=this.set(a.elements[4],a.elements[5],a.elements[6]).length();a=this.set(a.elements[8],a.elements[9],a.elements[10]).length();this.x=b;this.y=c;this.z=a;"
                << std::endl ;
            out
                << "return this},setFromMatrixColumn:function(a,b){var c=4*a,d=b.elements;this.x=d[c];this.y=d[c+1];this.z=d[c+2];return this},equals:function(a){return a.x===this.x&&a.y===this.y&&a.z===this.z},fromArray:function(a,b){void 0===b&&(b=0);this.x=a[b];this.y=a[b+1];this.z=a[b+2];return this},toArray:function(a,b){void 0===a&&(a=[]);void 0===b&&(b=0);a[b]=this.x;a[b+1]=this.y;a[b+2]=this.z;return a},fromAttribute:function(a,b,c){void 0===c&&(c=0);b=b*a.itemSize+c;this.x=a.array[b];this.y=a.array[b+1];this.z="
                << std::endl ;
            out
                << "a.array[b+2];return this},clone:function(){return new THREE.Vector3(this.x,this.y,this.z)}};THREE.Vector4=function(a,b,c,d){this.x=a||0;this.y=b||0;this.z=c||0;this.w=void 0!==d?d:1};"
                << std::endl ;
            out
                << "THREE.Vector4.prototype={constructor:THREE.Vector4,set:function(a,b,c,d){this.x=a;this.y=b;this.z=c;this.w=d;return this},setX:function(a){this.x=a;return this},setY:function(a){this.y=a;return this},setZ:function(a){this.z=a;return this},setW:function(a){this.w=a;return this},setComponent:function(a,b){switch(a){case 0:this.x=b;break;case 1:this.y=b;break;case 2:this.z=b;break;case 3:this.w=b;break;default:throw Error(\"index is out of range: \"+a);}},getComponent:function(a){switch(a){case 0:return this.x;"
                << std::endl ;
            out
                << "case 1:return this.y;case 2:return this.z;case 3:return this.w;default:throw Error(\"index is out of range: \"+a);}},copy:function(a){this.x=a.x;this.y=a.y;this.z=a.z;this.w=void 0!==a.w?a.w:1;return this},add:function(a,b){if(void 0!==b)return THREE.warn(\"THREE.Vector4: .add() now only accepts one argument. Use .addVectors( a, b ) instead.\"),this.addVectors(a,b);this.x+=a.x;this.y+=a.y;this.z+=a.z;this.w+=a.w;return this},addScalar:function(a){this.x+=a;this.y+=a;this.z+=a;this.w+=a;return this},addVectors:function(a,"
                << std::endl ;
            out
                << "b){this.x=a.x+b.x;this.y=a.y+b.y;this.z=a.z+b.z;this.w=a.w+b.w;return this},sub:function(a,b){if(void 0!==b)return THREE.warn(\"THREE.Vector4: .sub() now only accepts one argument. Use .subVectors( a, b ) instead.\"),this.subVectors(a,b);this.x-=a.x;this.y-=a.y;this.z-=a.z;this.w-=a.w;return this},subScalar:function(a){this.x-=a;this.y-=a;this.z-=a;this.w-=a;return this},subVectors:function(a,b){this.x=a.x-b.x;this.y=a.y-b.y;this.z=a.z-b.z;this.w=a.w-b.w;return this},multiplyScalar:function(a){this.x*="
                << std::endl ;
            out
                << "a;this.y*=a;this.z*=a;this.w*=a;return this},applyMatrix4:function(a){var b=this.x,c=this.y,d=this.z,e=this.w;a=a.elements;this.x=a[0]*b+a[4]*c+a[8]*d+a[12]*e;this.y=a[1]*b+a[5]*c+a[9]*d+a[13]*e;this.z=a[2]*b+a[6]*c+a[10]*d+a[14]*e;this.w=a[3]*b+a[7]*c+a[11]*d+a[15]*e;return this},divideScalar:function(a){0!==a?(a=1/a,this.x*=a,this.y*=a,this.z*=a,this.w*=a):(this.z=this.y=this.x=0,this.w=1);return this},setAxisAngleFromQuaternion:function(a){this.w=2*Math.acos(a.w);var b=Math.sqrt(1-a.w*a.w);1E-4>"
                << std::endl ;
            out
                << "b?(this.x=1,this.z=this.y=0):(this.x=a.x/b,this.y=a.y/b,this.z=a.z/b);return this},setAxisAngleFromRotationMatrix:function(a){var b,c,d;a=a.elements;var e=a[0];d=a[4];var f=a[8],g=a[1],h=a[5],k=a[9];c=a[2];b=a[6];var l=a[10];if(.01>Math.abs(d-g)&&.01>Math.abs(f-c)&&.01>Math.abs(k-b)){if(.1>Math.abs(d+g)&&.1>Math.abs(f+c)&&.1>Math.abs(k+b)&&.1>Math.abs(e+h+l-3))return this.set(1,0,0,0),this;a=Math.PI;e=(e+1)/2;h=(h+1)/2;l=(l+1)/2;d=(d+g)/4;f=(f+c)/4;k=(k+b)/4;e>h&&e>l?.01>e?(b=0,d=c=.707106781):(b="
                << std::endl ;
            out
                << "Math.sqrt(e),c=d/b,d=f/b):h>l?.01>h?(b=.707106781,c=0,d=.707106781):(c=Math.sqrt(h),b=d/c,d=k/c):.01>l?(c=b=.707106781,d=0):(d=Math.sqrt(l),b=f/d,c=k/d);this.set(b,c,d,a);return this}a=Math.sqrt((b-k)*(b-k)+(f-c)*(f-c)+(g-d)*(g-d));.001>Math.abs(a)&&(a=1);this.x=(b-k)/a;this.y=(f-c)/a;this.z=(g-d)/a;this.w=Math.acos((e+h+l-1)/2);return this},min:function(a){this.x>a.x&&(this.x=a.x);this.y>a.y&&(this.y=a.y);this.z>a.z&&(this.z=a.z);this.w>a.w&&(this.w=a.w);return this},max:function(a){this.x<a.x&&"
                << std::endl ;
            out
                << "(this.x=a.x);this.y<a.y&&(this.y=a.y);this.z<a.z&&(this.z=a.z);this.w<a.w&&(this.w=a.w);return this},clamp:function(a,b){this.x<a.x?this.x=a.x:this.x>b.x&&(this.x=b.x);this.y<a.y?this.y=a.y:this.y>b.y&&(this.y=b.y);this.z<a.z?this.z=a.z:this.z>b.z&&(this.z=b.z);this.w<a.w?this.w=a.w:this.w>b.w&&(this.w=b.w);return this},clampScalar:function(){var a,b;return function(c,d){void 0===a&&(a=new THREE.Vector4,b=new THREE.Vector4);a.set(c,c,c,c);b.set(d,d,d,d);return this.clamp(a,b)}}(),floor:function(){this.x="
                << std::endl ;
            out
                << "Math.floor(this.x);this.y=Math.floor(this.y);this.z=Math.floor(this.z);this.w=Math.floor(this.w);return this},ceil:function(){this.x=Math.ceil(this.x);this.y=Math.ceil(this.y);this.z=Math.ceil(this.z);this.w=Math.ceil(this.w);return this},round:function(){this.x=Math.round(this.x);this.y=Math.round(this.y);this.z=Math.round(this.z);this.w=Math.round(this.w);return this},roundToZero:function(){this.x=0>this.x?Math.ceil(this.x):Math.floor(this.x);this.y=0>this.y?Math.ceil(this.y):Math.floor(this.y);"
                << std::endl ;
            out
                << "this.z=0>this.z?Math.ceil(this.z):Math.floor(this.z);this.w=0>this.w?Math.ceil(this.w):Math.floor(this.w);return this},negate:function(){this.x=-this.x;this.y=-this.y;this.z=-this.z;this.w=-this.w;return this},dot:function(a){return this.x*a.x+this.y*a.y+this.z*a.z+this.w*a.w},lengthSq:function(){return this.x*this.x+this.y*this.y+this.z*this.z+this.w*this.w},length:function(){return Math.sqrt(this.x*this.x+this.y*this.y+this.z*this.z+this.w*this.w)},lengthManhattan:function(){return Math.abs(this.x)+"
                << std::endl ;
            out
                << "Math.abs(this.y)+Math.abs(this.z)+Math.abs(this.w)},normalize:function(){return this.divideScalar(this.length())},setLength:function(a){var b=this.length();0!==b&&a!==b&&this.multiplyScalar(a/b);return this},lerp:function(a,b){this.x+=(a.x-this.x)*b;this.y+=(a.y-this.y)*b;this.z+=(a.z-this.z)*b;this.w+=(a.w-this.w)*b;return this},lerpVectors:function(a,b,c){this.subVectors(b,a).multiplyScalar(c).add(a);return this},equals:function(a){return a.x===this.x&&a.y===this.y&&a.z===this.z&&a.w===this.w},"
                << std::endl ;
            out
                << "fromArray:function(a,b){void 0===b&&(b=0);this.x=a[b];this.y=a[b+1];this.z=a[b+2];this.w=a[b+3];return this},toArray:function(a,b){void 0===a&&(a=[]);void 0===b&&(b=0);a[b]=this.x;a[b+1]=this.y;a[b+2]=this.z;a[b+3]=this.w;return a},fromAttribute:function(a,b,c){void 0===c&&(c=0);b=b*a.itemSize+c;this.x=a.array[b];this.y=a.array[b+1];this.z=a.array[b+2];this.w=a.array[b+3];return this},clone:function(){return new THREE.Vector4(this.x,this.y,this.z,this.w)}};"
                << std::endl ;
            out
                << "THREE.Euler=function(a,b,c,d){this._x=a||0;this._y=b||0;this._z=c||0;this._order=d||THREE.Euler.DefaultOrder};THREE.Euler.RotationOrders=\"XYZ YZX ZXY XZY YXZ ZYX\".split(\" \");THREE.Euler.DefaultOrder=\"XYZ\";"
                << std::endl ;
            out
                << "THREE.Euler.prototype={constructor:THREE.Euler,_x:0,_y:0,_z:0,_order:THREE.Euler.DefaultOrder,get x(){return this._x},set x(a){this._x=a;this.onChangeCallback()},get y(){return this._y},set y(a){this._y=a;this.onChangeCallback()},get z(){return this._z},set z(a){this._z=a;this.onChangeCallback()},get order(){return this._order},set order(a){this._order=a;this.onChangeCallback()},set:function(a,b,c,d){this._x=a;this._y=b;this._z=c;this._order=d||this._order;this.onChangeCallback();return this},copy:function(a){this._x="
                << std::endl ;
            out
                << "a._x;this._y=a._y;this._z=a._z;this._order=a._order;this.onChangeCallback();return this},setFromRotationMatrix:function(a,b,c){var d=THREE.Math.clamp,e=a.elements;a=e[0];var f=e[4],g=e[8],h=e[1],k=e[5],l=e[9],p=e[2],q=e[6],e=e[10];b=b||this._order;\"XYZ\"===b?(this._y=Math.asin(d(g,-1,1)),.99999>Math.abs(g)?(this._x=Math.atan2(-l,e),this._z=Math.atan2(-f,a)):(this._x=Math.atan2(q,k),this._z=0)):\"YXZ\"===b?(this._x=Math.asin(-d(l,-1,1)),.99999>Math.abs(l)?(this._y=Math.atan2(g,e),this._z=Math.atan2(h,"
                << std::endl ;
            out
                << "k)):(this._y=Math.atan2(-p,a),this._z=0)):\"ZXY\"===b?(this._x=Math.asin(d(q,-1,1)),.99999>Math.abs(q)?(this._y=Math.atan2(-p,e),this._z=Math.atan2(-f,k)):(this._y=0,this._z=Math.atan2(h,a))):\"ZYX\"===b?(this._y=Math.asin(-d(p,-1,1)),.99999>Math.abs(p)?(this._x=Math.atan2(q,e),this._z=Math.atan2(h,a)):(this._x=0,this._z=Math.atan2(-f,k))):\"YZX\"===b?(this._z=Math.asin(d(h,-1,1)),.99999>Math.abs(h)?(this._x=Math.atan2(-l,k),this._y=Math.atan2(-p,a)):(this._x=0,this._y=Math.atan2(g,e))):\"XZY\"===b?(this._z="
                << std::endl ;
            out
                << "Math.asin(-d(f,-1,1)),.99999>Math.abs(f)?(this._x=Math.atan2(q,k),this._y=Math.atan2(g,a)):(this._x=Math.atan2(-l,e),this._y=0)):THREE.warn(\"THREE.Euler: .setFromRotationMatrix() given unsupported order: \"+b);this._order=b;if(!1!==c)this.onChangeCallback();return this},setFromQuaternion:function(){var a;return function(b,c,d){void 0===a&&(a=new THREE.Matrix4);a.makeRotationFromQuaternion(b);this.setFromRotationMatrix(a,c,d);return this}}(),setFromVector3:function(a,b){return this.set(a.x,a.y,a.z,"
                << std::endl ;
            out
                << "b||this._order)},reorder:function(){var a=new THREE.Quaternion;return function(b){a.setFromEuler(this);this.setFromQuaternion(a,b)}}(),equals:function(a){return a._x===this._x&&a._y===this._y&&a._z===this._z&&a._order===this._order},fromArray:function(a){this._x=a[0];this._y=a[1];this._z=a[2];void 0!==a[3]&&(this._order=a[3]);this.onChangeCallback();return this},toArray:function(a,b){void 0===a&&(a=[]);void 0===b&&(b=0);a[b]=this._x;a[b+1]=this._y;a[b+2]=this._z;a[b+3]=this._order;return a},toVector3:function(a){return a?"
                << std::endl ;
            out
                << "a.set(this._x,this._y,this._z):new THREE.Vector3(this._x,this._y,this._z)},onChange:function(a){this.onChangeCallback=a;return this},onChangeCallback:function(){},clone:function(){return new THREE.Euler(this._x,this._y,this._z,this._order)}};THREE.Line3=function(a,b){this.start=void 0!==a?a:new THREE.Vector3;this.end=void 0!==b?b:new THREE.Vector3};"
                << std::endl ;
            out
                << "THREE.Line3.prototype={constructor:THREE.Line3,set:function(a,b){this.start.copy(a);this.end.copy(b);return this},copy:function(a){this.start.copy(a.start);this.end.copy(a.end);return this},center:function(a){return(a||new THREE.Vector3).addVectors(this.start,this.end).multiplyScalar(.5)},delta:function(a){return(a||new THREE.Vector3).subVectors(this.end,this.start)},distanceSq:function(){return this.start.distanceToSquared(this.end)},distance:function(){return this.start.distanceTo(this.end)},at:function(a,"
                << std::endl ;
            out
                << "b){var c=b||new THREE.Vector3;return this.delta(c).multiplyScalar(a).add(this.start)},closestPointToPointParameter:function(){var a=new THREE.Vector3,b=new THREE.Vector3;return function(c,d){a.subVectors(c,this.start);b.subVectors(this.end,this.start);var e=b.dot(b),e=b.dot(a)/e;d&&(e=THREE.Math.clamp(e,0,1));return e}}(),closestPointToPoint:function(a,b,c){a=this.closestPointToPointParameter(a,b);c=c||new THREE.Vector3;return this.delta(c).multiplyScalar(a).add(this.start)},applyMatrix4:function(a){this.start.applyMatrix4(a);"
                << std::endl ;
            out
                << "this.end.applyMatrix4(a);return this},equals:function(a){return a.start.equals(this.start)&&a.end.equals(this.end)},clone:function(){return(new THREE.Line3).copy(this)}};THREE.Box2=function(a,b){this.min=void 0!==a?a:new THREE.Vector2(Infinity,Infinity);this.max=void 0!==b?b:new THREE.Vector2(-Infinity,-Infinity)};"
                << std::endl ;
            out
                << "THREE.Box2.prototype={constructor:THREE.Box2,set:function(a,b){this.min.copy(a);this.max.copy(b);return this},setFromPoints:function(a){this.makeEmpty();for(var b=0,c=a.length;b<c;b++)this.expandByPoint(a[b]);return this},setFromCenterAndSize:function(){var a=new THREE.Vector2;return function(b,c){var d=a.copy(c).multiplyScalar(.5);this.min.copy(b).sub(d);this.max.copy(b).add(d);return this}}(),copy:function(a){this.min.copy(a.min);this.max.copy(a.max);return this},makeEmpty:function(){this.min.x="
                << std::endl ;
            out
                << "this.min.y=Infinity;this.max.x=this.max.y=-Infinity;return this},empty:function(){return this.max.x<this.min.x||this.max.y<this.min.y},center:function(a){return(a||new THREE.Vector2).addVectors(this.min,this.max).multiplyScalar(.5)},size:function(a){return(a||new THREE.Vector2).subVectors(this.max,this.min)},expandByPoint:function(a){this.min.min(a);this.max.max(a);return this},expandByVector:function(a){this.min.sub(a);this.max.add(a);return this},expandByScalar:function(a){this.min.addScalar(-a);"
                << std::endl ;
            out
                << "this.max.addScalar(a);return this},containsPoint:function(a){return a.x<this.min.x||a.x>this.max.x||a.y<this.min.y||a.y>this.max.y?!1:!0},containsBox:function(a){return this.min.x<=a.min.x&&a.max.x<=this.max.x&&this.min.y<=a.min.y&&a.max.y<=this.max.y?!0:!1},getParameter:function(a,b){return(b||new THREE.Vector2).set((a.x-this.min.x)/(this.max.x-this.min.x),(a.y-this.min.y)/(this.max.y-this.min.y))},isIntersectionBox:function(a){return a.max.x<this.min.x||a.min.x>this.max.x||a.max.y<this.min.y||a.min.y>"
                << std::endl ;
            out
                << "this.max.y?!1:!0},clampPoint:function(a,b){return(b||new THREE.Vector2).copy(a).clamp(this.min,this.max)},distanceToPoint:function(){var a=new THREE.Vector2;return function(b){return a.copy(b).clamp(this.min,this.max).sub(b).length()}}(),intersect:function(a){this.min.max(a.min);this.max.min(a.max);return this},union:function(a){this.min.min(a.min);this.max.max(a.max);return this},translate:function(a){this.min.add(a);this.max.add(a);return this},equals:function(a){return a.min.equals(this.min)&&"
                << std::endl ;
            out
                << "a.max.equals(this.max)},clone:function(){return(new THREE.Box2).copy(this)}};THREE.Box3=function(a,b){this.min=void 0!==a?a:new THREE.Vector3(Infinity,Infinity,Infinity);this.max=void 0!==b?b:new THREE.Vector3(-Infinity,-Infinity,-Infinity)};"
                << std::endl ;
            out
                << "THREE.Box3.prototype={constructor:THREE.Box3,set:function(a,b){this.min.copy(a);this.max.copy(b);return this},setFromPoints:function(a){this.makeEmpty();for(var b=0,c=a.length;b<c;b++)this.expandByPoint(a[b]);return this},setFromCenterAndSize:function(){var a=new THREE.Vector3;return function(b,c){var d=a.copy(c).multiplyScalar(.5);this.min.copy(b).sub(d);this.max.copy(b).add(d);return this}}(),setFromObject:function(){var a=new THREE.Vector3;return function(b){var c=this;b.updateMatrixWorld(!0);"
                << std::endl ;
            out
                << "this.makeEmpty();b.traverse(function(b){var e=b.geometry;if(void 0!==e)if(e instanceof THREE.Geometry)for(var f=e.vertices,e=0,g=f.length;e<g;e++)a.copy(f[e]),a.applyMatrix4(b.matrixWorld),c.expandByPoint(a);else if(e instanceof THREE.BufferGeometry&&void 0!==e.attributes.position)for(f=e.attributes.position.array,e=0,g=f.length;e<g;e+=3)a.set(f[e],f[e+1],f[e+2]),a.applyMatrix4(b.matrixWorld),c.expandByPoint(a)});return this}}(),copy:function(a){this.min.copy(a.min);this.max.copy(a.max);return this},"
                << std::endl ;
            out
                << "makeEmpty:function(){this.min.x=this.min.y=this.min.z=Infinity;this.max.x=this.max.y=this.max.z=-Infinity;return this},empty:function(){return this.max.x<this.min.x||this.max.y<this.min.y||this.max.z<this.min.z},center:function(a){return(a||new THREE.Vector3).addVectors(this.min,this.max).multiplyScalar(.5)},size:function(a){return(a||new THREE.Vector3).subVectors(this.max,this.min)},expandByPoint:function(a){this.min.min(a);this.max.max(a);return this},expandByVector:function(a){this.min.sub(a);"
                << std::endl ;
            out
                << "this.max.add(a);return this},expandByScalar:function(a){this.min.addScalar(-a);this.max.addScalar(a);return this},containsPoint:function(a){return a.x<this.min.x||a.x>this.max.x||a.y<this.min.y||a.y>this.max.y||a.z<this.min.z||a.z>this.max.z?!1:!0},containsBox:function(a){return this.min.x<=a.min.x&&a.max.x<=this.max.x&&this.min.y<=a.min.y&&a.max.y<=this.max.y&&this.min.z<=a.min.z&&a.max.z<=this.max.z?!0:!1},getParameter:function(a,b){return(b||new THREE.Vector3).set((a.x-this.min.x)/(this.max.x-"
                << std::endl ;
            out
                << "this.min.x),(a.y-this.min.y)/(this.max.y-this.min.y),(a.z-this.min.z)/(this.max.z-this.min.z))},isIntersectionBox:function(a){return a.max.x<this.min.x||a.min.x>this.max.x||a.max.y<this.min.y||a.min.y>this.max.y||a.max.z<this.min.z||a.min.z>this.max.z?!1:!0},clampPoint:function(a,b){return(b||new THREE.Vector3).copy(a).clamp(this.min,this.max)},distanceToPoint:function(){var a=new THREE.Vector3;return function(b){return a.copy(b).clamp(this.min,this.max).sub(b).length()}}(),getBoundingSphere:function(){var a="
                << std::endl ;
            out
                << "new THREE.Vector3;return function(b){b=b||new THREE.Sphere;b.center=this.center();b.radius=.5*this.size(a).length();return b}}(),intersect:function(a){this.min.max(a.min);this.max.min(a.max);return this},union:function(a){this.min.min(a.min);this.max.max(a.max);return this},applyMatrix4:function(){var a=[new THREE.Vector3,new THREE.Vector3,new THREE.Vector3,new THREE.Vector3,new THREE.Vector3,new THREE.Vector3,new THREE.Vector3,new THREE.Vector3];return function(b){a[0].set(this.min.x,this.min.y,"
                << std::endl ;
            out
                << "this.min.z).applyMatrix4(b);a[1].set(this.min.x,this.min.y,this.max.z).applyMatrix4(b);a[2].set(this.min.x,this.max.y,this.min.z).applyMatrix4(b);a[3].set(this.min.x,this.max.y,this.max.z).applyMatrix4(b);a[4].set(this.max.x,this.min.y,this.min.z).applyMatrix4(b);a[5].set(this.max.x,this.min.y,this.max.z).applyMatrix4(b);a[6].set(this.max.x,this.max.y,this.min.z).applyMatrix4(b);a[7].set(this.max.x,this.max.y,this.max.z).applyMatrix4(b);this.makeEmpty();this.setFromPoints(a);return this}}(),translate:function(a){this.min.add(a);"
                << std::endl ;
            out
                << "this.max.add(a);return this},equals:function(a){return a.min.equals(this.min)&&a.max.equals(this.max)},clone:function(){return(new THREE.Box3).copy(this)}};THREE.Matrix3=function(){this.elements=new Float32Array([1,0,0,0,1,0,0,0,1]);0<arguments.length&&THREE.error(\"THREE.Matrix3: the constructor no longer reads arguments. use .set() instead.\")};"
                << std::endl ;
            out
                << "THREE.Matrix3.prototype={constructor:THREE.Matrix3,set:function(a,b,c,d,e,f,g,h,k){var l=this.elements;l[0]=a;l[3]=b;l[6]=c;l[1]=d;l[4]=e;l[7]=f;l[2]=g;l[5]=h;l[8]=k;return this},identity:function(){this.set(1,0,0,0,1,0,0,0,1);return this},copy:function(a){a=a.elements;this.set(a[0],a[3],a[6],a[1],a[4],a[7],a[2],a[5],a[8]);return this},multiplyVector3:function(a){THREE.warn(\"THREE.Matrix3: .multiplyVector3() has been removed. Use vector.applyMatrix3( matrix ) instead.\");return a.applyMatrix3(this)},"
                << std::endl ;
            out
                << "multiplyVector3Array:function(a){THREE.warn(\"THREE.Matrix3: .multiplyVector3Array() has been renamed. Use matrix.applyToVector3Array( array ) instead.\");return this.applyToVector3Array(a)},applyToVector3Array:function(){var a=new THREE.Vector3;return function(b,c,d){void 0===c&&(c=0);void 0===d&&(d=b.length);for(var e=0;e<d;e+=3,c+=3)a.x=b[c],a.y=b[c+1],a.z=b[c+2],a.applyMatrix3(this),b[c]=a.x,b[c+1]=a.y,b[c+2]=a.z;return b}}(),multiplyScalar:function(a){var b=this.elements;b[0]*=a;b[3]*=a;b[6]*="
                << std::endl ;
            out
                << "a;b[1]*=a;b[4]*=a;b[7]*=a;b[2]*=a;b[5]*=a;b[8]*=a;return this},determinant:function(){var a=this.elements,b=a[0],c=a[1],d=a[2],e=a[3],f=a[4],g=a[5],h=a[6],k=a[7],a=a[8];return b*f*a-b*g*k-c*e*a+c*g*h+d*e*k-d*f*h},getInverse:function(a,b){var c=a.elements,d=this.elements;d[0]=c[10]*c[5]-c[6]*c[9];d[1]=-c[10]*c[1]+c[2]*c[9];d[2]=c[6]*c[1]-c[2]*c[5];d[3]=-c[10]*c[4]+c[6]*c[8];d[4]=c[10]*c[0]-c[2]*c[8];d[5]=-c[6]*c[0]+c[2]*c[4];d[6]=c[9]*c[4]-c[5]*c[8];d[7]=-c[9]*c[0]+c[1]*c[8];d[8]=c[5]*c[0]-c[1]*c[4];"
                << std::endl ;
            out
                << "c=c[0]*d[0]+c[1]*d[3]+c[2]*d[6];if(0===c){if(b)throw Error(\"Matrix3.getInverse(): can't invert matrix, determinant is 0\");THREE.warn(\"Matrix3.getInverse(): can't invert matrix, determinant is 0\");this.identity();return this}this.multiplyScalar(1/c);return this},transpose:function(){var a,b=this.elements;a=b[1];b[1]=b[3];b[3]=a;a=b[2];b[2]=b[6];b[6]=a;a=b[5];b[5]=b[7];b[7]=a;return this},flattenToArrayOffset:function(a,b){var c=this.elements;a[b]=c[0];a[b+1]=c[1];a[b+2]=c[2];a[b+3]=c[3];a[b+4]=c[4];"
                << std::endl ;
            out
                << "a[b+5]=c[5];a[b+6]=c[6];a[b+7]=c[7];a[b+8]=c[8];return a},getNormalMatrix:function(a){this.getInverse(a).transpose();return this},transposeIntoArray:function(a){var b=this.elements;a[0]=b[0];a[1]=b[3];a[2]=b[6];a[3]=b[1];a[4]=b[4];a[5]=b[7];a[6]=b[2];a[7]=b[5];a[8]=b[8];return this},fromArray:function(a){this.elements.set(a);return this},toArray:function(){var a=this.elements;return[a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7],a[8]]},clone:function(){return(new THREE.Matrix3).fromArray(this.elements)}};"
                << std::endl ;
            out
                << "THREE.Matrix4=function(){this.elements=new Float32Array([1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]);0<arguments.length&&THREE.error(\"THREE.Matrix4: the constructor no longer reads arguments. use .set() instead.\")};"
                << std::endl ;
            out
                << "THREE.Matrix4.prototype={constructor:THREE.Matrix4,set:function(a,b,c,d,e,f,g,h,k,l,p,q,n,t,r,s){var u=this.elements;u[0]=a;u[4]=b;u[8]=c;u[12]=d;u[1]=e;u[5]=f;u[9]=g;u[13]=h;u[2]=k;u[6]=l;u[10]=p;u[14]=q;u[3]=n;u[7]=t;u[11]=r;u[15]=s;return this},identity:function(){this.set(1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1);return this},copy:function(a){this.elements.set(a.elements);return this},extractPosition:function(a){THREE.warn(\"THREE.Matrix4: .extractPosition() has been renamed to .copyPosition().\");return this.copyPosition(a)},"
                << std::endl ;
            out
                << "copyPosition:function(a){var b=this.elements;a=a.elements;b[12]=a[12];b[13]=a[13];b[14]=a[14];return this},extractBasis:function(a,b,c){var d=this.elements;a.set(d[0],d[1],d[2]);b.set(d[4],d[5],d[6]);c.set(d[8],d[9],d[10]);return this},makeBasis:function(a,b,c){this.set(a.x,b.x,c.x,0,a.y,b.y,c.y,0,a.z,b.z,c.z,0,0,0,0,1);return this},extractRotation:function(){var a=new THREE.Vector3;return function(b){var c=this.elements;b=b.elements;var d=1/a.set(b[0],b[1],b[2]).length(),e=1/a.set(b[4],b[5],b[6]).length(),"
                << std::endl ;
            out
                << "f=1/a.set(b[8],b[9],b[10]).length();c[0]=b[0]*d;c[1]=b[1]*d;c[2]=b[2]*d;c[4]=b[4]*e;c[5]=b[5]*e;c[6]=b[6]*e;c[8]=b[8]*f;c[9]=b[9]*f;c[10]=b[10]*f;return this}}(),makeRotationFromEuler:function(a){!1===a instanceof THREE.Euler&&THREE.error(\"THREE.Matrix: .makeRotationFromEuler() now expects a Euler rotation rather than a Vector3 and order.\");var b=this.elements,c=a.x,d=a.y,e=a.z,f=Math.cos(c),c=Math.sin(c),g=Math.cos(d),d=Math.sin(d),h=Math.cos(e),e=Math.sin(e);if(\"XYZ\"===a.order){a=f*h;var k=f*e,"
                << std::endl ;
            out
                << "l=c*h,p=c*e;b[0]=g*h;b[4]=-g*e;b[8]=d;b[1]=k+l*d;b[5]=a-p*d;b[9]=-c*g;b[2]=p-a*d;b[6]=l+k*d;b[10]=f*g}else\"YXZ\"===a.order?(a=g*h,k=g*e,l=d*h,p=d*e,b[0]=a+p*c,b[4]=l*c-k,b[8]=f*d,b[1]=f*e,b[5]=f*h,b[9]=-c,b[2]=k*c-l,b[6]=p+a*c,b[10]=f*g):\"ZXY\"===a.order?(a=g*h,k=g*e,l=d*h,p=d*e,b[0]=a-p*c,b[4]=-f*e,b[8]=l+k*c,b[1]=k+l*c,b[5]=f*h,b[9]=p-a*c,b[2]=-f*d,b[6]=c,b[10]=f*g):\"ZYX\"===a.order?(a=f*h,k=f*e,l=c*h,p=c*e,b[0]=g*h,b[4]=l*d-k,b[8]=a*d+p,b[1]=g*e,b[5]=p*d+a,b[9]=k*d-l,b[2]=-d,b[6]=c*g,b[10]=f*g):\"YZX\"==="
                << std::endl ;
            out
                << "a.order?(a=f*g,k=f*d,l=c*g,p=c*d,b[0]=g*h,b[4]=p-a*e,b[8]=l*e+k,b[1]=e,b[5]=f*h,b[9]=-c*h,b[2]=-d*h,b[6]=k*e+l,b[10]=a-p*e):\"XZY\"===a.order&&(a=f*g,k=f*d,l=c*g,p=c*d,b[0]=g*h,b[4]=-e,b[8]=d*h,b[1]=a*e+p,b[5]=f*h,b[9]=k*e-l,b[2]=l*e-k,b[6]=c*h,b[10]=p*e+a);b[3]=0;b[7]=0;b[11]=0;b[12]=0;b[13]=0;b[14]=0;b[15]=1;return this},setRotationFromQuaternion:function(a){THREE.warn(\"THREE.Matrix4: .setRotationFromQuaternion() has been renamed to .makeRotationFromQuaternion().\");return this.makeRotationFromQuaternion(a)},"
                << std::endl ;
            out
                << "makeRotationFromQuaternion:function(a){var b=this.elements,c=a.x,d=a.y,e=a.z,f=a.w,g=c+c,h=d+d,k=e+e;a=c*g;var l=c*h,c=c*k,p=d*h,d=d*k,e=e*k,g=f*g,h=f*h,f=f*k;b[0]=1-(p+e);b[4]=l-f;b[8]=c+h;b[1]=l+f;b[5]=1-(a+e);b[9]=d-g;b[2]=c-h;b[6]=d+g;b[10]=1-(a+p);b[3]=0;b[7]=0;b[11]=0;b[12]=0;b[13]=0;b[14]=0;b[15]=1;return this},lookAt:function(){var a=new THREE.Vector3,b=new THREE.Vector3,c=new THREE.Vector3;return function(d,e,f){var g=this.elements;c.subVectors(d,e).normalize();0===c.length()&&(c.z=1);a.crossVectors(f,"
                << std::endl ;
            out
                << "c).normalize();0===a.length()&&(c.x+=1E-4,a.crossVectors(f,c).normalize());b.crossVectors(c,a);g[0]=a.x;g[4]=b.x;g[8]=c.x;g[1]=a.y;g[5]=b.y;g[9]=c.y;g[2]=a.z;g[6]=b.z;g[10]=c.z;return this}}(),multiply:function(a,b){return void 0!==b?(THREE.warn(\"THREE.Matrix4: .multiply() now only accepts one argument. Use .multiplyMatrices( a, b ) instead.\"),this.multiplyMatrices(a,b)):this.multiplyMatrices(this,a)},multiplyMatrices:function(a,b){var c=a.elements,d=b.elements,e=this.elements,f=c[0],g=c[4],h=c[8],"
                << std::endl ;
            out
                << "k=c[12],l=c[1],p=c[5],q=c[9],n=c[13],t=c[2],r=c[6],s=c[10],u=c[14],v=c[3],x=c[7],D=c[11],c=c[15],w=d[0],y=d[4],A=d[8],E=d[12],G=d[1],F=d[5],z=d[9],I=d[13],U=d[2],M=d[6],H=d[10],L=d[14],P=d[3],N=d[7],R=d[11],d=d[15];e[0]=f*w+g*G+h*U+k*P;e[4]=f*y+g*F+h*M+k*N;e[8]=f*A+g*z+h*H+k*R;e[12]=f*E+g*I+h*L+k*d;e[1]=l*w+p*G+q*U+n*P;e[5]=l*y+p*F+q*M+n*N;e[9]=l*A+p*z+q*H+n*R;e[13]=l*E+p*I+q*L+n*d;e[2]=t*w+r*G+s*U+u*P;e[6]=t*y+r*F+s*M+u*N;e[10]=t*A+r*z+s*H+u*R;e[14]=t*E+r*I+s*L+u*d;e[3]=v*w+x*G+D*U+c*P;e[7]=v*y+"
                << std::endl ;
            out
                << "x*F+D*M+c*N;e[11]=v*A+x*z+D*H+c*R;e[15]=v*E+x*I+D*L+c*d;return this},multiplyToArray:function(a,b,c){var d=this.elements;this.multiplyMatrices(a,b);c[0]=d[0];c[1]=d[1];c[2]=d[2];c[3]=d[3];c[4]=d[4];c[5]=d[5];c[6]=d[6];c[7]=d[7];c[8]=d[8];c[9]=d[9];c[10]=d[10];c[11]=d[11];c[12]=d[12];c[13]=d[13];c[14]=d[14];c[15]=d[15];return this},multiplyScalar:function(a){var b=this.elements;b[0]*=a;b[4]*=a;b[8]*=a;b[12]*=a;b[1]*=a;b[5]*=a;b[9]*=a;b[13]*=a;b[2]*=a;b[6]*=a;b[10]*=a;b[14]*=a;b[3]*=a;b[7]*=a;b[11]*="
                << std::endl ;
            out
                << "a;b[15]*=a;return this},multiplyVector3:function(a){THREE.warn(\"THREE.Matrix4: .multiplyVector3() has been removed. Use vector.applyMatrix4( matrix ) or vector.applyProjection( matrix ) instead.\");return a.applyProjection(this)},multiplyVector4:function(a){THREE.warn(\"THREE.Matrix4: .multiplyVector4() has been removed. Use vector.applyMatrix4( matrix ) instead.\");return a.applyMatrix4(this)},multiplyVector3Array:function(a){THREE.warn(\"THREE.Matrix4: .multiplyVector3Array() has been renamed. Use matrix.applyToVector3Array( array ) instead.\");"
                << std::endl ;
            out
                << "return this.applyToVector3Array(a)},applyToVector3Array:function(){var a=new THREE.Vector3;return function(b,c,d){void 0===c&&(c=0);void 0===d&&(d=b.length);for(var e=0;e<d;e+=3,c+=3)a.x=b[c],a.y=b[c+1],a.z=b[c+2],a.applyMatrix4(this),b[c]=a.x,b[c+1]=a.y,b[c+2]=a.z;return b}}(),rotateAxis:function(a){THREE.warn(\"THREE.Matrix4: .rotateAxis() has been removed. Use Vector3.transformDirection( matrix ) instead.\");a.transformDirection(this)},crossVector:function(a){THREE.warn(\"THREE.Matrix4: .crossVector() has been removed. Use vector.applyMatrix4( matrix ) instead.\");"
                << std::endl ;
            out
                << "return a.applyMatrix4(this)},determinant:function(){var a=this.elements,b=a[0],c=a[4],d=a[8],e=a[12],f=a[1],g=a[5],h=a[9],k=a[13],l=a[2],p=a[6],q=a[10],n=a[14];return a[3]*(+e*h*p-d*k*p-e*g*q+c*k*q+d*g*n-c*h*n)+a[7]*(+b*h*n-b*k*q+e*f*q-d*f*n+d*k*l-e*h*l)+a[11]*(+b*k*p-b*g*n-e*f*p+c*f*n+e*g*l-c*k*l)+a[15]*(-d*g*l-b*h*p+b*g*q+d*f*p-c*f*q+c*h*l)},transpose:function(){var a=this.elements,b;b=a[1];a[1]=a[4];a[4]=b;b=a[2];a[2]=a[8];a[8]=b;b=a[6];a[6]=a[9];a[9]=b;b=a[3];a[3]=a[12];a[12]=b;b=a[7];a[7]=a[13];"
                << std::endl ;
            out
                << "a[13]=b;b=a[11];a[11]=a[14];a[14]=b;return this},flattenToArrayOffset:function(a,b){var c=this.elements;a[b]=c[0];a[b+1]=c[1];a[b+2]=c[2];a[b+3]=c[3];a[b+4]=c[4];a[b+5]=c[5];a[b+6]=c[6];a[b+7]=c[7];a[b+8]=c[8];a[b+9]=c[9];a[b+10]=c[10];a[b+11]=c[11];a[b+12]=c[12];a[b+13]=c[13];a[b+14]=c[14];a[b+15]=c[15];return a},getPosition:function(){var a=new THREE.Vector3;return function(){THREE.warn(\"THREE.Matrix4: .getPosition() has been removed. Use Vector3.setFromMatrixPosition( matrix ) instead.\");var b="
                << std::endl ;
            out
                << "this.elements;return a.set(b[12],b[13],b[14])}}(),setPosition:function(a){var b=this.elements;b[12]=a.x;b[13]=a.y;b[14]=a.z;return this},getInverse:function(a,b){var c=this.elements,d=a.elements,e=d[0],f=d[4],g=d[8],h=d[12],k=d[1],l=d[5],p=d[9],q=d[13],n=d[2],t=d[6],r=d[10],s=d[14],u=d[3],v=d[7],x=d[11],d=d[15];c[0]=p*s*v-q*r*v+q*t*x-l*s*x-p*t*d+l*r*d;c[4]=h*r*v-g*s*v-h*t*x+f*s*x+g*t*d-f*r*d;c[8]=g*q*v-h*p*v+h*l*x-f*q*x-g*l*d+f*p*d;c[12]=h*p*t-g*q*t-h*l*r+f*q*r+g*l*s-f*p*s;c[1]=q*r*u-p*s*u-q*n*x+"
                << std::endl ;
            out
                << "k*s*x+p*n*d-k*r*d;c[5]=g*s*u-h*r*u+h*n*x-e*s*x-g*n*d+e*r*d;c[9]=h*p*u-g*q*u-h*k*x+e*q*x+g*k*d-e*p*d;c[13]=g*q*n-h*p*n+h*k*r-e*q*r-g*k*s+e*p*s;c[2]=l*s*u-q*t*u+q*n*v-k*s*v-l*n*d+k*t*d;c[6]=h*t*u-f*s*u-h*n*v+e*s*v+f*n*d-e*t*d;c[10]=f*q*u-h*l*u+h*k*v-e*q*v-f*k*d+e*l*d;c[14]=h*l*n-f*q*n-h*k*t+e*q*t+f*k*s-e*l*s;c[3]=p*t*u-l*r*u-p*n*v+k*r*v+l*n*x-k*t*x;c[7]=f*r*u-g*t*u+g*n*v-e*r*v-f*n*x+e*t*x;c[11]=g*l*u-f*p*u-g*k*v+e*p*v+f*k*x-e*l*x;c[15]=f*p*n-g*l*n+g*k*t-e*p*t-f*k*r+e*l*r;c=e*c[0]+k*c[4]+n*c[8]+u*c[12];"
                << std::endl ;
            out
                << "if(0==c){if(b)throw Error(\"THREE.Matrix4.getInverse(): can't invert matrix, determinant is 0\");THREE.warn(\"THREE.Matrix4.getInverse(): can't invert matrix, determinant is 0\");this.identity();return this}this.multiplyScalar(1/c);return this},translate:function(a){THREE.error(\"THREE.Matrix4: .translate() has been removed.\")},rotateX:function(a){THREE.error(\"THREE.Matrix4: .rotateX() has been removed.\")},rotateY:function(a){THREE.error(\"THREE.Matrix4: .rotateY() has been removed.\")},rotateZ:function(a){THREE.error(\"THREE.Matrix4: .rotateZ() has been removed.\")},"
                << std::endl ;
            out
                << "rotateByAxis:function(a,b){THREE.error(\"THREE.Matrix4: .rotateByAxis() has been removed.\")},scale:function(a){var b=this.elements,c=a.x,d=a.y;a=a.z;b[0]*=c;b[4]*=d;b[8]*=a;b[1]*=c;b[5]*=d;b[9]*=a;b[2]*=c;b[6]*=d;b[10]*=a;b[3]*=c;b[7]*=d;b[11]*=a;return this},getMaxScaleOnAxis:function(){var a=this.elements;return Math.sqrt(Math.max(a[0]*a[0]+a[1]*a[1]+a[2]*a[2],Math.max(a[4]*a[4]+a[5]*a[5]+a[6]*a[6],a[8]*a[8]+a[9]*a[9]+a[10]*a[10])))},makeTranslation:function(a,b,c){this.set(1,0,0,a,0,1,0,b,0,0,1,"
                << std::endl ;
            out
                << "c,0,0,0,1);return this},makeRotationX:function(a){var b=Math.cos(a);a=Math.sin(a);this.set(1,0,0,0,0,b,-a,0,0,a,b,0,0,0,0,1);return this},makeRotationY:function(a){var b=Math.cos(a);a=Math.sin(a);this.set(b,0,a,0,0,1,0,0,-a,0,b,0,0,0,0,1);return this},makeRotationZ:function(a){var b=Math.cos(a);a=Math.sin(a);this.set(b,-a,0,0,a,b,0,0,0,0,1,0,0,0,0,1);return this},makeRotationAxis:function(a,b){var c=Math.cos(b),d=Math.sin(b),e=1-c,f=a.x,g=a.y,h=a.z,k=e*f,l=e*g;this.set(k*f+c,k*g-d*h,k*h+d*g,0,k*g+"
                << std::endl ;
            out
                << "d*h,l*g+c,l*h-d*f,0,k*h-d*g,l*h+d*f,e*h*h+c,0,0,0,0,1);return this},makeScale:function(a,b,c){this.set(a,0,0,0,0,b,0,0,0,0,c,0,0,0,0,1);return this},compose:function(a,b,c){this.makeRotationFromQuaternion(b);this.scale(c);this.setPosition(a);return this},decompose:function(){var a=new THREE.Vector3,b=new THREE.Matrix4;return function(c,d,e){var f=this.elements,g=a.set(f[0],f[1],f[2]).length(),h=a.set(f[4],f[5],f[6]).length(),k=a.set(f[8],f[9],f[10]).length();0>this.determinant()&&(g=-g);c.x=f[12];"
                << std::endl ;
            out
                << "c.y=f[13];c.z=f[14];b.elements.set(this.elements);c=1/g;var f=1/h,l=1/k;b.elements[0]*=c;b.elements[1]*=c;b.elements[2]*=c;b.elements[4]*=f;b.elements[5]*=f;b.elements[6]*=f;b.elements[8]*=l;b.elements[9]*=l;b.elements[10]*=l;d.setFromRotationMatrix(b);e.x=g;e.y=h;e.z=k;return this}}(),makeFrustum:function(a,b,c,d,e,f){var g=this.elements;g[0]=2*e/(b-a);g[4]=0;g[8]=(b+a)/(b-a);g[12]=0;g[1]=0;g[5]=2*e/(d-c);g[9]=(d+c)/(d-c);g[13]=0;g[2]=0;g[6]=0;g[10]=-(f+e)/(f-e);g[14]=-2*f*e/(f-e);g[3]=0;g[7]=0;"
                << std::endl ;
            out
                << "g[11]=-1;g[15]=0;return this},makePerspective:function(a,b,c,d){a=c*Math.tan(THREE.Math.degToRad(.5*a));var e=-a;return this.makeFrustum(e*b,a*b,e,a,c,d)},makeOrthographic:function(a,b,c,d,e,f){var g=this.elements,h=b-a,k=c-d,l=f-e;g[0]=2/h;g[4]=0;g[8]=0;g[12]=-((b+a)/h);g[1]=0;g[5]=2/k;g[9]=0;g[13]=-((c+d)/k);g[2]=0;g[6]=0;g[10]=-2/l;g[14]=-((f+e)/l);g[3]=0;g[7]=0;g[11]=0;g[15]=1;return this},fromArray:function(a){this.elements.set(a);return this},toArray:function(){var a=this.elements;return[a[0],"
                << std::endl ;
            out
                << "a[1],a[2],a[3],a[4],a[5],a[6],a[7],a[8],a[9],a[10],a[11],a[12],a[13],a[14],a[15]]},clone:function(){return(new THREE.Matrix4).fromArray(this.elements)}};THREE.Ray=function(a,b){this.origin=void 0!==a?a:new THREE.Vector3;this.direction=void 0!==b?b:new THREE.Vector3};"
                << std::endl ;
            out
                << "THREE.Ray.prototype={constructor:THREE.Ray,set:function(a,b){this.origin.copy(a);this.direction.copy(b);return this},copy:function(a){this.origin.copy(a.origin);this.direction.copy(a.direction);return this},at:function(a,b){return(b||new THREE.Vector3).copy(this.direction).multiplyScalar(a).add(this.origin)},recast:function(){var a=new THREE.Vector3;return function(b){this.origin.copy(this.at(b,a));return this}}(),closestPointToPoint:function(a,b){var c=b||new THREE.Vector3;c.subVectors(a,this.origin);"
                << std::endl ;
            out
                << "var d=c.dot(this.direction);return 0>d?c.copy(this.origin):c.copy(this.direction).multiplyScalar(d).add(this.origin)},distanceToPoint:function(){var a=new THREE.Vector3;return function(b){var c=a.subVectors(b,this.origin).dot(this.direction);if(0>c)return this.origin.distanceTo(b);a.copy(this.direction).multiplyScalar(c).add(this.origin);return a.distanceTo(b)}}(),distanceSqToSegment:function(){var a=new THREE.Vector3,b=new THREE.Vector3,c=new THREE.Vector3;return function(d,e,f,g){a.copy(d).add(e).multiplyScalar(.5);"
                << std::endl ;
            out
                << "b.copy(e).sub(d).normalize();c.copy(this.origin).sub(a);var h=.5*d.distanceTo(e),k=-this.direction.dot(b),l=c.dot(this.direction),p=-c.dot(b),q=c.lengthSq(),n=Math.abs(1-k*k),t;0<n?(d=k*p-l,e=k*l-p,t=h*n,0<=d?e>=-t?e<=t?(h=1/n,d*=h,e*=h,k=d*(d+k*e+2*l)+e*(k*d+e+2*p)+q):(e=h,d=Math.max(0,-(k*e+l)),k=-d*d+e*(e+2*p)+q):(e=-h,d=Math.max(0,-(k*e+l)),k=-d*d+e*(e+2*p)+q):e<=-t?(d=Math.max(0,-(-k*h+l)),e=0<d?-h:Math.min(Math.max(-h,-p),h),k=-d*d+e*(e+2*p)+q):e<=t?(d=0,e=Math.min(Math.max(-h,-p),h),k=e*(e+"
                << std::endl ;
            out
                << "2*p)+q):(d=Math.max(0,-(k*h+l)),e=0<d?h:Math.min(Math.max(-h,-p),h),k=-d*d+e*(e+2*p)+q)):(e=0<k?-h:h,d=Math.max(0,-(k*e+l)),k=-d*d+e*(e+2*p)+q);f&&f.copy(this.direction).multiplyScalar(d).add(this.origin);g&&g.copy(b).multiplyScalar(e).add(a);return k}}(),isIntersectionSphere:function(a){return this.distanceToPoint(a.center)<=a.radius},intersectSphere:function(){var a=new THREE.Vector3;return function(b,c){a.subVectors(b.center,this.origin);var d=a.dot(this.direction),e=a.dot(a)-d*d,f=b.radius*b.radius;"
                << std::endl ;
            out
                << "if(e>f)return null;f=Math.sqrt(f-e);e=d-f;d+=f;return 0>e&&0>d?null:0>e?this.at(d,c):this.at(e,c)}}(),isIntersectionPlane:function(a){var b=a.distanceToPoint(this.origin);return 0===b||0>a.normal.dot(this.direction)*b?!0:!1},distanceToPlane:function(a){var b=a.normal.dot(this.direction);if(0==b)return 0==a.distanceToPoint(this.origin)?0:null;a=-(this.origin.dot(a.normal)+a.constant)/b;return 0<=a?a:null},intersectPlane:function(a,b){var c=this.distanceToPlane(a);return null===c?null:this.at(c,b)},"
                << std::endl ;
            out
                << "isIntersectionBox:function(){var a=new THREE.Vector3;return function(b){return null!==this.intersectBox(b,a)}}(),intersectBox:function(a,b){var c,d,e,f,g;d=1/this.direction.x;f=1/this.direction.y;g=1/this.direction.z;var h=this.origin;0<=d?(c=(a.min.x-h.x)*d,d*=a.max.x-h.x):(c=(a.max.x-h.x)*d,d*=a.min.x-h.x);0<=f?(e=(a.min.y-h.y)*f,f*=a.max.y-h.y):(e=(a.max.y-h.y)*f,f*=a.min.y-h.y);if(c>f||e>d)return null;if(e>c||c!==c)c=e;if(f<d||d!==d)d=f;0<=g?(e=(a.min.z-h.z)*g,g*=a.max.z-h.z):(e=(a.max.z-h.z)*"
                << std::endl ;
            out
                << "g,g*=a.min.z-h.z);if(c>g||e>d)return null;if(e>c||c!==c)c=e;if(g<d||d!==d)d=g;return 0>d?null:this.at(0<=c?c:d,b)},intersectTriangle:function(){var a=new THREE.Vector3,b=new THREE.Vector3,c=new THREE.Vector3,d=new THREE.Vector3;return function(e,f,g,h,k){b.subVectors(f,e);c.subVectors(g,e);d.crossVectors(b,c);f=this.direction.dot(d);if(0<f){if(h)return null;h=1}else if(0>f)h=-1,f=-f;else return null;a.subVectors(this.origin,e);e=h*this.direction.dot(c.crossVectors(a,c));if(0>e)return null;g=h*this.direction.dot(b.cross(a));"
                << std::endl ;
            out
                << "if(0>g||e+g>f)return null;e=-h*a.dot(d);return 0>e?null:this.at(e/f,k)}}(),applyMatrix4:function(a){this.direction.add(this.origin).applyMatrix4(a);this.origin.applyMatrix4(a);this.direction.sub(this.origin);this.direction.normalize();return this},equals:function(a){return a.origin.equals(this.origin)&&a.direction.equals(this.direction)},clone:function(){return(new THREE.Ray).copy(this)}};THREE.Sphere=function(a,b){this.center=void 0!==a?a:new THREE.Vector3;this.radius=void 0!==b?b:0};"
                << std::endl ;
            out
                << "THREE.Sphere.prototype={constructor:THREE.Sphere,set:function(a,b){this.center.copy(a);this.radius=b;return this},setFromPoints:function(){var a=new THREE.Box3;return function(b,c){var d=this.center;void 0!==c?d.copy(c):a.setFromPoints(b).center(d);for(var e=0,f=0,g=b.length;f<g;f++)e=Math.max(e,d.distanceToSquared(b[f]));this.radius=Math.sqrt(e);return this}}(),copy:function(a){this.center.copy(a.center);this.radius=a.radius;return this},empty:function(){return 0>=this.radius},containsPoint:function(a){return a.distanceToSquared(this.center)<="
                << std::endl ;
            out
                << "this.radius*this.radius},distanceToPoint:function(a){return a.distanceTo(this.center)-this.radius},intersectsSphere:function(a){var b=this.radius+a.radius;return a.center.distanceToSquared(this.center)<=b*b},clampPoint:function(a,b){var c=this.center.distanceToSquared(a),d=b||new THREE.Vector3;d.copy(a);c>this.radius*this.radius&&(d.sub(this.center).normalize(),d.multiplyScalar(this.radius).add(this.center));return d},getBoundingBox:function(a){a=a||new THREE.Box3;a.set(this.center,this.center);a.expandByScalar(this.radius);"
                << std::endl ;
            out
                << "return a},applyMatrix4:function(a){this.center.applyMatrix4(a);this.radius*=a.getMaxScaleOnAxis();return this},translate:function(a){this.center.add(a);return this},equals:function(a){return a.center.equals(this.center)&&a.radius===this.radius},clone:function(){return(new THREE.Sphere).copy(this)}};"
                << std::endl ;
            out
                << "THREE.Frustum=function(a,b,c,d,e,f){this.planes=[void 0!==a?a:new THREE.Plane,void 0!==b?b:new THREE.Plane,void 0!==c?c:new THREE.Plane,void 0!==d?d:new THREE.Plane,void 0!==e?e:new THREE.Plane,void 0!==f?f:new THREE.Plane]};"
                << std::endl ;
            out
                << "THREE.Frustum.prototype={constructor:THREE.Frustum,set:function(a,b,c,d,e,f){var g=this.planes;g[0].copy(a);g[1].copy(b);g[2].copy(c);g[3].copy(d);g[4].copy(e);g[5].copy(f);return this},copy:function(a){for(var b=this.planes,c=0;6>c;c++)b[c].copy(a.planes[c]);return this},setFromMatrix:function(a){var b=this.planes,c=a.elements;a=c[0];var d=c[1],e=c[2],f=c[3],g=c[4],h=c[5],k=c[6],l=c[7],p=c[8],q=c[9],n=c[10],t=c[11],r=c[12],s=c[13],u=c[14],c=c[15];b[0].setComponents(f-a,l-g,t-p,c-r).normalize();b[1].setComponents(f+"
                << std::endl ;
            out
                << "a,l+g,t+p,c+r).normalize();b[2].setComponents(f+d,l+h,t+q,c+s).normalize();b[3].setComponents(f-d,l-h,t-q,c-s).normalize();b[4].setComponents(f-e,l-k,t-n,c-u).normalize();b[5].setComponents(f+e,l+k,t+n,c+u).normalize();return this},intersectsObject:function(){var a=new THREE.Sphere;return function(b){var c=b.geometry;null===c.boundingSphere&&c.computeBoundingSphere();a.copy(c.boundingSphere);a.applyMatrix4(b.matrixWorld);return this.intersectsSphere(a)}}(),intersectsSphere:function(a){var b=this.planes,"
                << std::endl ;
            out
                << "c=a.center;a=-a.radius;for(var d=0;6>d;d++)if(b[d].distanceToPoint(c)<a)return!1;return!0},intersectsBox:function(){var a=new THREE.Vector3,b=new THREE.Vector3;return function(c){for(var d=this.planes,e=0;6>e;e++){var f=d[e];a.x=0<f.normal.x?c.min.x:c.max.x;b.x=0<f.normal.x?c.max.x:c.min.x;a.y=0<f.normal.y?c.min.y:c.max.y;b.y=0<f.normal.y?c.max.y:c.min.y;a.z=0<f.normal.z?c.min.z:c.max.z;b.z=0<f.normal.z?c.max.z:c.min.z;var g=f.distanceToPoint(a),f=f.distanceToPoint(b);if(0>g&&0>f)return!1}return!0}}(),"
                << std::endl ;
            out
                << "containsPoint:function(a){for(var b=this.planes,c=0;6>c;c++)if(0>b[c].distanceToPoint(a))return!1;return!0},clone:function(){return(new THREE.Frustum).copy(this)}};THREE.Plane=function(a,b){this.normal=void 0!==a?a:new THREE.Vector3(1,0,0);this.constant=void 0!==b?b:0};"
                << std::endl ;
            out
                << "THREE.Plane.prototype={constructor:THREE.Plane,set:function(a,b){this.normal.copy(a);this.constant=b;return this},setComponents:function(a,b,c,d){this.normal.set(a,b,c);this.constant=d;return this},setFromNormalAndCoplanarPoint:function(a,b){this.normal.copy(a);this.constant=-b.dot(this.normal);return this},setFromCoplanarPoints:function(){var a=new THREE.Vector3,b=new THREE.Vector3;return function(c,d,e){d=a.subVectors(e,d).cross(b.subVectors(c,d)).normalize();this.setFromNormalAndCoplanarPoint(d,"
                << std::endl ;
            out
                << "c);return this}}(),copy:function(a){this.normal.copy(a.normal);this.constant=a.constant;return this},normalize:function(){var a=1/this.normal.length();this.normal.multiplyScalar(a);this.constant*=a;return this},negate:function(){this.constant*=-1;this.normal.negate();return this},distanceToPoint:function(a){return this.normal.dot(a)+this.constant},distanceToSphere:function(a){return this.distanceToPoint(a.center)-a.radius},projectPoint:function(a,b){return this.orthoPoint(a,b).sub(a).negate()},orthoPoint:function(a,"
                << std::endl ;
            out
                << "b){var c=this.distanceToPoint(a);return(b||new THREE.Vector3).copy(this.normal).multiplyScalar(c)},isIntersectionLine:function(a){var b=this.distanceToPoint(a.start);a=this.distanceToPoint(a.end);return 0>b&&0<a||0>a&&0<b},intersectLine:function(){var a=new THREE.Vector3;return function(b,c){var d=c||new THREE.Vector3,e=b.delta(a),f=this.normal.dot(e);if(0==f){if(0==this.distanceToPoint(b.start))return d.copy(b.start)}else return f=-(b.start.dot(this.normal)+this.constant)/f,0>f||1<f?void 0:d.copy(e).multiplyScalar(f).add(b.start)}}(),"
                << std::endl ;
            out
                << "coplanarPoint:function(a){return(a||new THREE.Vector3).copy(this.normal).multiplyScalar(-this.constant)},applyMatrix4:function(){var a=new THREE.Vector3,b=new THREE.Vector3,c=new THREE.Matrix3;return function(d,e){var f=e||c.getNormalMatrix(d),f=a.copy(this.normal).applyMatrix3(f),g=this.coplanarPoint(b);g.applyMatrix4(d);this.setFromNormalAndCoplanarPoint(f,g);return this}}(),translate:function(a){this.constant-=a.dot(this.normal);return this},equals:function(a){return a.normal.equals(this.normal)&&"
                << std::endl ;
            out
                << "a.constant==this.constant},clone:function(){return(new THREE.Plane).copy(this)}};"
                << std::endl ;
            out
                << "THREE.Math={generateUUID:function(){var a=\"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz\".split(\"\"),b=Array(36),c=0,d;return function(){for(var e=0;36>e;e++)8==e||13==e||18==e||23==e?b[e]=\"-\":14==e?b[e]=\"4\":(2>=c&&(c=33554432+16777216*Math.random()|0),d=c&15,c>>=4,b[e]=a[19==e?d&3|8:d]);return b.join(\"\")}}(),clamp:function(a,b,c){return a<b?b:a>c?c:a},clampBottom:function(a,b){return a<b?b:a},mapLinear:function(a,b,c,d,e){return d+(a-b)*(e-d)/(c-b)},smoothstep:function(a,b,c){if(a<="
                << std::endl ;
            out
                << "b)return 0;if(a>=c)return 1;a=(a-b)/(c-b);return a*a*(3-2*a)},smootherstep:function(a,b,c){if(a<=b)return 0;if(a>=c)return 1;a=(a-b)/(c-b);return a*a*a*(a*(6*a-15)+10)},random16:function(){return(65280*Math.random()+255*Math.random())/65535},randInt:function(a,b){return Math.floor(this.randFloat(a,b))},randFloat:function(a,b){return a+Math.random()*(b-a)},randFloatSpread:function(a){return a*(.5-Math.random())},degToRad:function(){var a=Math.PI/180;return function(b){return b*a}}(),radToDeg:function(){var a="
                << std::endl ;
            out
                << "180/Math.PI;return function(b){return b*a}}(),isPowerOfTwo:function(a){return 0===(a&a-1)&&0!==a},nextPowerOfTwo:function(a){a--;a|=a>>1;a|=a>>2;a|=a>>4;a|=a>>8;a|=a>>16;a++;return a}};"
                << std::endl ;
            out
                << "THREE.Spline=function(a){function b(a,b,c,d,e,f,g){a=.5*(c-a);d=.5*(d-b);return(2*(b-c)+a+d)*g+(-3*(b-c)-2*a-d)*f+a*e+b}this.points=a;var c=[],d={x:0,y:0,z:0},e,f,g,h,k,l,p,q,n;this.initFromArray=function(a){this.points=[];for(var b=0;b<a.length;b++)this.points[b]={x:a[b][0],y:a[b][1],z:a[b][2]}};this.getPoint=function(a){e=(this.points.length-1)*a;f=Math.floor(e);g=e-f;c[0]=0===f?f:f-1;c[1]=f;c[2]=f>this.points.length-2?this.points.length-1:f+1;c[3]=f>this.points.length-3?this.points.length-1:f+"
                << std::endl ;
            out
                << "2;l=this.points[c[0]];p=this.points[c[1]];q=this.points[c[2]];n=this.points[c[3]];h=g*g;k=g*h;d.x=b(l.x,p.x,q.x,n.x,g,h,k);d.y=b(l.y,p.y,q.y,n.y,g,h,k);d.z=b(l.z,p.z,q.z,n.z,g,h,k);return d};this.getControlPointsArray=function(){var a,b,c=this.points.length,d=[];for(a=0;a<c;a++)b=this.points[a],d[a]=[b.x,b.y,b.z];return d};this.getLength=function(a){var b,c,d,e=b=b=0,f=new THREE.Vector3,g=new THREE.Vector3,h=[],k=0;h[0]=0;a||(a=100);c=this.points.length*a;f.copy(this.points[0]);for(a=1;a<c;a++)b="
                << std::endl ;
            out
                << "a/c,d=this.getPoint(b),g.copy(d),k+=g.distanceTo(f),f.copy(d),b*=this.points.length-1,b=Math.floor(b),b!=e&&(h[b]=k,e=b);h[h.length]=k;return{chunks:h,total:k}};this.reparametrizeByArcLength=function(a){var b,c,d,e,f,g,h=[],k=new THREE.Vector3,n=this.getLength();h.push(k.copy(this.points[0]).clone());for(b=1;b<this.points.length;b++){c=n.chunks[b]-n.chunks[b-1];g=Math.ceil(a*c/n.total);e=(b-1)/(this.points.length-1);f=b/(this.points.length-1);for(c=1;c<g-1;c++)d=e+1/g*c*(f-e),d=this.getPoint(d),h.push(k.copy(d).clone());"
                << std::endl ;
            out
                << "h.push(k.copy(this.points[b]).clone())}this.points=h}};THREE.Triangle=function(a,b,c){this.a=void 0!==a?a:new THREE.Vector3;this.b=void 0!==b?b:new THREE.Vector3;this.c=void 0!==c?c:new THREE.Vector3};THREE.Triangle.normal=function(){var a=new THREE.Vector3;return function(b,c,d,e){e=e||new THREE.Vector3;e.subVectors(d,c);a.subVectors(b,c);e.cross(a);b=e.lengthSq();return 0<b?e.multiplyScalar(1/Math.sqrt(b)):e.set(0,0,0)}}();"
                << std::endl ;
            out
                << "THREE.Triangle.barycoordFromPoint=function(){var a=new THREE.Vector3,b=new THREE.Vector3,c=new THREE.Vector3;return function(d,e,f,g,h){a.subVectors(g,e);b.subVectors(f,e);c.subVectors(d,e);d=a.dot(a);e=a.dot(b);f=a.dot(c);var k=b.dot(b);g=b.dot(c);var l=d*k-e*e;h=h||new THREE.Vector3;if(0==l)return h.set(-2,-1,-1);l=1/l;k=(k*f-e*g)*l;d=(d*g-e*f)*l;return h.set(1-k-d,d,k)}}();"
                << std::endl ;
            out
                << "THREE.Triangle.containsPoint=function(){var a=new THREE.Vector3;return function(b,c,d,e){b=THREE.Triangle.barycoordFromPoint(b,c,d,e,a);return 0<=b.x&&0<=b.y&&1>=b.x+b.y}}();"
                << std::endl ;
            out
                << "THREE.Triangle.prototype={constructor:THREE.Triangle,set:function(a,b,c){this.a.copy(a);this.b.copy(b);this.c.copy(c);return this},setFromPointsAndIndices:function(a,b,c,d){this.a.copy(a[b]);this.b.copy(a[c]);this.c.copy(a[d]);return this},copy:function(a){this.a.copy(a.a);this.b.copy(a.b);this.c.copy(a.c);return this},area:function(){var a=new THREE.Vector3,b=new THREE.Vector3;return function(){a.subVectors(this.c,this.b);b.subVectors(this.a,this.b);return.5*a.cross(b).length()}}(),midpoint:function(a){return(a||"
                << std::endl ;
            out
                << "new THREE.Vector3).addVectors(this.a,this.b).add(this.c).multiplyScalar(1/3)},normal:function(a){return THREE.Triangle.normal(this.a,this.b,this.c,a)},plane:function(a){return(a||new THREE.Plane).setFromCoplanarPoints(this.a,this.b,this.c)},barycoordFromPoint:function(a,b){return THREE.Triangle.barycoordFromPoint(a,this.a,this.b,this.c,b)},containsPoint:function(a){return THREE.Triangle.containsPoint(a,this.a,this.b,this.c)},equals:function(a){return a.a.equals(this.a)&&a.b.equals(this.b)&&a.c.equals(this.c)},"
                << std::endl ;
            out
                << "clone:function(){return(new THREE.Triangle).copy(this)}};THREE.Clock=function(a){this.autoStart=void 0!==a?a:!0;this.elapsedTime=this.oldTime=this.startTime=0;this.running=!1};"
                << std::endl ;
            out
                << "THREE.Clock.prototype={constructor:THREE.Clock,start:function(){this.oldTime=this.startTime=void 0!==self.performance&&void 0!==self.performance.now?self.performance.now():Date.now();this.running=!0},stop:function(){this.getElapsedTime();this.running=!1},getElapsedTime:function(){this.getDelta();return this.elapsedTime},getDelta:function(){var a=0;this.autoStart&&!this.running&&this.start();if(this.running){var b=void 0!==self.performance&&void 0!==self.performance.now?self.performance.now():Date.now(),"
                << std::endl ;
            out
                << "a=.001*(b-this.oldTime);this.oldTime=b;this.elapsedTime+=a}return a}};THREE.EventDispatcher=function(){};"
                << std::endl ;
            out
                << "THREE.EventDispatcher.prototype={constructor:THREE.EventDispatcher,apply:function(a){a.addEventListener=THREE.EventDispatcher.prototype.addEventListener;a.hasEventListener=THREE.EventDispatcher.prototype.hasEventListener;a.removeEventListener=THREE.EventDispatcher.prototype.removeEventListener;a.dispatchEvent=THREE.EventDispatcher.prototype.dispatchEvent},addEventListener:function(a,b){void 0===this._listeners&&(this._listeners={});var c=this._listeners;void 0===c[a]&&(c[a]=[]);-1===c[a].indexOf(b)&&"
                << std::endl ;
            out
                << "c[a].push(b)},hasEventListener:function(a,b){if(void 0===this._listeners)return!1;var c=this._listeners;return void 0!==c[a]&&-1!==c[a].indexOf(b)?!0:!1},removeEventListener:function(a,b){if(void 0!==this._listeners){var c=this._listeners[a];if(void 0!==c){var d=c.indexOf(b);-1!==d&&c.splice(d,1)}}},dispatchEvent:function(a){if(void 0!==this._listeners){var b=this._listeners[a.type];if(void 0!==b){a.target=this;for(var c=[],d=b.length,e=0;e<d;e++)c[e]=b[e];for(e=0;e<d;e++)c[e].call(this,a)}}}};"
                << std::endl ;
            out
                << "(function(a){a.Raycaster=function(b,c,f,g){this.ray=new a.Ray(b,c);this.near=f||0;this.far=g||Infinity;this.params={Sprite:{},Mesh:{},PointCloud:{threshold:1},LOD:{},Line:{}}};var b=function(a,b){return a.distance-b.distance},c=function(a,b,f,g){a.raycast(b,f);if(!0===g){a=a.children;g=0;for(var h=a.length;g<h;g++)c(a[g],b,f,!0)}};a.Raycaster.prototype={constructor:a.Raycaster,precision:1E-4,linePrecision:1,set:function(a,b){this.ray.set(a,b)},setFromCamera:function(b,c){c instanceof a.PerspectiveCamera?"
                << std::endl ;
            out
                << "(this.ray.origin.copy(c.position),this.ray.direction.set(b.x,b.y,.5).unproject(c).sub(c.position).normalize()):c instanceof a.OrthographicCamera?(this.ray.origin.set(b.x,b.y,-1).unproject(c),this.ray.direction.set(0,0,-1).transformDirection(c.matrixWorld)):a.error(\"THREE.Raycaster: Unsupported camera type.\")},intersectObject:function(a,e){var f=[];c(a,this,f,e);f.sort(b);return f},intersectObjects:function(d,e){var f=[];if(!1===d instanceof Array)return a.warn(\"THREE.Raycaster.intersectObjects: objects is not an Array.\"),"
                << std::endl ;
            out
                << "f;for(var g=0,h=d.length;g<h;g++)c(d[g],this,f,e);f.sort(b);return f}}})(THREE);"
                << std::endl ;
            out
                << "THREE.Object3D=function(){Object.defineProperty(this,\"id\",{value:THREE.Object3DIdCount++});this.uuid=THREE.Math.generateUUID();this.name=\"\";this.type=\"Object3D\";this.parent=void 0;this.children=[];this.up=THREE.Object3D.DefaultUp.clone();var a=new THREE.Vector3,b=new THREE.Euler,c=new THREE.Quaternion,d=new THREE.Vector3(1,1,1);b.onChange(function(){c.setFromEuler(b,!1)});c.onChange(function(){b.setFromQuaternion(c,void 0,!1)});Object.defineProperties(this,{position:{enumerable:!0,value:a},rotation:{enumerable:!0,"
                << std::endl ;
            out
                << "value:b},quaternion:{enumerable:!0,value:c},scale:{enumerable:!0,value:d}});this.rotationAutoUpdate=!0;this.matrix=new THREE.Matrix4;this.matrixWorld=new THREE.Matrix4;this.matrixAutoUpdate=!0;this.matrixWorldNeedsUpdate=!1;this.visible=!0;this.receiveShadow=this.castShadow=!1;this.frustumCulled=!0;this.renderOrder=0;this.userData={}};THREE.Object3D.DefaultUp=new THREE.Vector3(0,1,0);"
                << std::endl ;
            out
                << "THREE.Object3D.prototype={constructor:THREE.Object3D,get eulerOrder(){THREE.warn(\"THREE.Object3D: .eulerOrder has been moved to .rotation.order.\");return this.rotation.order},set eulerOrder(a){THREE.warn(\"THREE.Object3D: .eulerOrder has been moved to .rotation.order.\");this.rotation.order=a},get useQuaternion(){THREE.warn(\"THREE.Object3D: .useQuaternion has been removed. The library now uses quaternions by default.\")},set useQuaternion(a){THREE.warn(\"THREE.Object3D: .useQuaternion has been removed. The library now uses quaternions by default.\")},"
                << std::endl ;
            out
                << "applyMatrix:function(a){this.matrix.multiplyMatrices(a,this.matrix);this.matrix.decompose(this.position,this.quaternion,this.scale)},setRotationFromAxisAngle:function(a,b){this.quaternion.setFromAxisAngle(a,b)},setRotationFromEuler:function(a){this.quaternion.setFromEuler(a,!0)},setRotationFromMatrix:function(a){this.quaternion.setFromRotationMatrix(a)},setRotationFromQuaternion:function(a){this.quaternion.copy(a)},rotateOnAxis:function(){var a=new THREE.Quaternion;return function(b,c){a.setFromAxisAngle(b,"
                << std::endl ;
            out
                << "c);this.quaternion.multiply(a);return this}}(),rotateX:function(){var a=new THREE.Vector3(1,0,0);return function(b){return this.rotateOnAxis(a,b)}}(),rotateY:function(){var a=new THREE.Vector3(0,1,0);return function(b){return this.rotateOnAxis(a,b)}}(),rotateZ:function(){var a=new THREE.Vector3(0,0,1);return function(b){return this.rotateOnAxis(a,b)}}(),translateOnAxis:function(){var a=new THREE.Vector3;return function(b,c){a.copy(b).applyQuaternion(this.quaternion);this.position.add(a.multiplyScalar(c));"
                << std::endl ;
            out
                << "return this}}(),translate:function(a,b){THREE.warn(\"THREE.Object3D: .translate() has been removed. Use .translateOnAxis( axis, distance ) instead.\");return this.translateOnAxis(b,a)},translateX:function(){var a=new THREE.Vector3(1,0,0);return function(b){return this.translateOnAxis(a,b)}}(),translateY:function(){var a=new THREE.Vector3(0,1,0);return function(b){return this.translateOnAxis(a,b)}}(),translateZ:function(){var a=new THREE.Vector3(0,0,1);return function(b){return this.translateOnAxis(a,"
                << std::endl ;
            out
                << "b)}}(),localToWorld:function(a){return a.applyMatrix4(this.matrixWorld)},worldToLocal:function(){var a=new THREE.Matrix4;return function(b){return b.applyMatrix4(a.getInverse(this.matrixWorld))}}(),lookAt:function(){var a=new THREE.Matrix4;return function(b){a.lookAt(b,this.position,this.up);this.quaternion.setFromRotationMatrix(a)}}(),add:function(a){if(1<arguments.length){for(var b=0;b<arguments.length;b++)this.add(arguments[b]);return this}if(a===this)return THREE.error(\"THREE.Object3D.add: object can't be added as a child of itself.\","
                << std::endl ;
            out
                << "a),this;a instanceof THREE.Object3D?(void 0!==a.parent&&a.parent.remove(a),a.parent=this,a.dispatchEvent({type:\"added\"}),this.children.push(a)):THREE.error(\"THREE.Object3D.add: object not an instance of THREE.Object3D.\",a);return this},remove:function(a){if(1<arguments.length)for(var b=0;b<arguments.length;b++)this.remove(arguments[b]);b=this.children.indexOf(a);-1!==b&&(a.parent=void 0,a.dispatchEvent({type:\"removed\"}),this.children.splice(b,1))},getChildByName:function(a){THREE.warn(\"THREE.Object3D: .getChildByName() has been renamed to .getObjectByName().\");"
                << std::endl ;
            out
                << "return this.getObjectByName(a)},getObjectById:function(a){return this.getObjectByProperty(\"id\",a)},getObjectByName:function(a){return this.getObjectByProperty(\"name\",a)},getObjectByProperty:function(a,b){if(this[a]===b)return this;for(var c=0,d=this.children.length;c<d;c++){var e=this.children[c].getObjectByProperty(a,b);if(void 0!==e)return e}},getWorldPosition:function(a){a=a||new THREE.Vector3;this.updateMatrixWorld(!0);return a.setFromMatrixPosition(this.matrixWorld)},getWorldQuaternion:function(){var a="
                << std::endl ;
            out
                << "new THREE.Vector3,b=new THREE.Vector3;return function(c){c=c||new THREE.Quaternion;this.updateMatrixWorld(!0);this.matrixWorld.decompose(a,c,b);return c}}(),getWorldRotation:function(){var a=new THREE.Quaternion;return function(b){b=b||new THREE.Euler;this.getWorldQuaternion(a);return b.setFromQuaternion(a,this.rotation.order,!1)}}(),getWorldScale:function(){var a=new THREE.Vector3,b=new THREE.Quaternion;return function(c){c=c||new THREE.Vector3;this.updateMatrixWorld(!0);this.matrixWorld.decompose(a,"
                << std::endl ;
            out
                << "b,c);return c}}(),getWorldDirection:function(){var a=new THREE.Quaternion;return function(b){b=b||new THREE.Vector3;this.getWorldQuaternion(a);return b.set(0,0,1).applyQuaternion(a)}}(),raycast:function(){},traverse:function(a){a(this);for(var b=0,c=this.children.length;b<c;b++)this.children[b].traverse(a)},traverseVisible:function(a){if(!1!==this.visible){a(this);for(var b=0,c=this.children.length;b<c;b++)this.children[b].traverseVisible(a)}},traverseAncestors:function(a){this.parent&&(a(this.parent),"
                << std::endl ;
            out
                << "this.parent.traverseAncestors(a))},updateMatrix:function(){this.matrix.compose(this.position,this.quaternion,this.scale);this.matrixWorldNeedsUpdate=!0},updateMatrixWorld:function(a){!0===this.matrixAutoUpdate&&this.updateMatrix();if(!0===this.matrixWorldNeedsUpdate||!0===a)void 0===this.parent?this.matrixWorld.copy(this.matrix):this.matrixWorld.multiplyMatrices(this.parent.matrixWorld,this.matrix),this.matrixWorldNeedsUpdate=!1,a=!0;for(var b=0,c=this.children.length;b<c;b++)this.children[b].updateMatrixWorld(a)},"
                << std::endl ;
            out
                << "toJSON:function(){var a={metadata:{version:4.3,type:\"Object\",generator:\"ObjectExporter\"}},b={},c={},d=function(b){void 0===a.materials&&(a.materials=[]);if(void 0===c[b.uuid]){var d=b.toJSON();delete d.metadata;c[b.uuid]=d;a.materials.push(d)}return b.uuid},e=function(c){var g={};g.uuid=c.uuid;g.type=c.type;\"\"!==c.name&&(g.name=c.name);\"{}\"!==JSON.stringify(c.userData)&&(g.userData=c.userData);!0!==c.visible&&(g.visible=c.visible);if(c instanceof THREE.PerspectiveCamera)g.fov=c.fov,g.aspect=c.aspect,"
                << std::endl ;
            out
                << "g.near=c.near,g.far=c.far;else if(c instanceof THREE.OrthographicCamera)g.left=c.left,g.right=c.right,g.top=c.top,g.bottom=c.bottom,g.near=c.near,g.far=c.far;else if(c instanceof THREE.AmbientLight)g.color=c.color.getHex();else if(c instanceof THREE.DirectionalLight)g.color=c.color.getHex(),g.intensity=c.intensity;else if(c instanceof THREE.PointLight)g.color=c.color.getHex(),g.intensity=c.intensity,g.distance=c.distance,g.decay=c.decay;else if(c instanceof THREE.SpotLight)g.color=c.color.getHex(),"
                << std::endl ;
            out
                << "g.intensity=c.intensity,g.distance=c.distance,g.angle=c.angle,g.exponent=c.exponent,g.decay=c.decay;else if(c instanceof THREE.HemisphereLight)g.color=c.color.getHex(),g.groundColor=c.groundColor.getHex();else if(c instanceof THREE.Mesh||c instanceof THREE.Line||c instanceof THREE.PointCloud){var h=c.geometry;void 0===a.geometries&&(a.geometries=[]);if(void 0===b[h.uuid]){var k=h.toJSON();delete k.metadata;b[h.uuid]=k;a.geometries.push(k)}g.geometry=h.uuid;g.material=d(c.material);c instanceof THREE.Line&&"
                << std::endl ;
            out
                << "(g.mode=c.mode)}else c instanceof THREE.Sprite&&(g.material=d(c.material));g.matrix=c.matrix.toArray();if(0<c.children.length)for(g.children=[],h=0;h<c.children.length;h++)g.children.push(e(c.children[h]));return g};a.object=e(this);return a},clone:function(a,b){void 0===a&&(a=new THREE.Object3D);void 0===b&&(b=!0);a.name=this.name;a.up.copy(this.up);a.position.copy(this.position);a.quaternion.copy(this.quaternion);a.scale.copy(this.scale);a.rotationAutoUpdate=this.rotationAutoUpdate;a.matrix.copy(this.matrix);"
                << std::endl ;
            out
                << "a.matrixWorld.copy(this.matrixWorld);a.matrixAutoUpdate=this.matrixAutoUpdate;a.matrixWorldNeedsUpdate=this.matrixWorldNeedsUpdate;a.visible=this.visible;a.castShadow=this.castShadow;a.receiveShadow=this.receiveShadow;a.frustumCulled=this.frustumCulled;a.userData=JSON.parse(JSON.stringify(this.userData));if(!0===b)for(var c=0;c<this.children.length;c++)a.add(this.children[c].clone());return a}};THREE.EventDispatcher.prototype.apply(THREE.Object3D.prototype);THREE.Object3DIdCount=0;"
                << std::endl ;
            out
                << "THREE.Face3=function(a,b,c,d,e,f){this.a=a;this.b=b;this.c=c;this.normal=d instanceof THREE.Vector3?d:new THREE.Vector3;this.vertexNormals=d instanceof Array?d:[];this.color=e instanceof THREE.Color?e:new THREE.Color;this.vertexColors=e instanceof Array?e:[];this.vertexTangents=[];this.materialIndex=void 0!==f?f:0};"
                << std::endl ;
            out
                << "THREE.Face3.prototype={constructor:THREE.Face3,clone:function(){var a=new THREE.Face3(this.a,this.b,this.c);a.normal.copy(this.normal);a.color.copy(this.color);a.materialIndex=this.materialIndex;for(var b=0,c=this.vertexNormals.length;b<c;b++)a.vertexNormals[b]=this.vertexNormals[b].clone();b=0;for(c=this.vertexColors.length;b<c;b++)a.vertexColors[b]=this.vertexColors[b].clone();b=0;for(c=this.vertexTangents.length;b<c;b++)a.vertexTangents[b]=this.vertexTangents[b].clone();return a}};"
                << std::endl ;
            out
                << "THREE.Face4=function(a,b,c,d,e,f,g){THREE.warn(\"THREE.Face4 has been removed. A THREE.Face3 will be created instead.\");return new THREE.Face3(a,b,c,e,f,g)};THREE.BufferAttribute=function(a,b){this.array=a;this.itemSize=b;this.needsUpdate=!1};"
                << std::endl ;
            out
                << "THREE.BufferAttribute.prototype={constructor:THREE.BufferAttribute,get length(){return this.array.length},copyAt:function(a,b,c){a*=this.itemSize;c*=b.itemSize;for(var d=0,e=this.itemSize;d<e;d++)this.array[a+d]=b.array[c+d];return this},set:function(a,b){void 0===b&&(b=0);this.array.set(a,b);return this},setX:function(a,b){this.array[a*this.itemSize]=b;return this},setY:function(a,b){this.array[a*this.itemSize+1]=b;return this},setZ:function(a,b){this.array[a*this.itemSize+2]=b;return this},setXY:function(a,"
                << std::endl ;
            out
                << "b,c){a*=this.itemSize;this.array[a]=b;this.array[a+1]=c;return this},setXYZ:function(a,b,c,d){a*=this.itemSize;this.array[a]=b;this.array[a+1]=c;this.array[a+2]=d;return this},setXYZW:function(a,b,c,d,e){a*=this.itemSize;this.array[a]=b;this.array[a+1]=c;this.array[a+2]=d;this.array[a+3]=e;return this},clone:function(){return new THREE.BufferAttribute(new this.array.constructor(this.array),this.itemSize)}};"
                << std::endl ;
            out
                << "THREE.Int8Attribute=function(a,b){THREE.warn(\"THREE.Int8Attribute has been removed. Use THREE.BufferAttribute( array, itemSize ) instead.\");return new THREE.BufferAttribute(a,b)};THREE.Uint8Attribute=function(a,b){THREE.warn(\"THREE.Uint8Attribute has been removed. Use THREE.BufferAttribute( array, itemSize ) instead.\");return new THREE.BufferAttribute(a,b)};"
                << std::endl ;
            out
                << "THREE.Uint8ClampedAttribute=function(a,b){THREE.warn(\"THREE.Uint8ClampedAttribute has been removed. Use THREE.BufferAttribute( array, itemSize ) instead.\");return new THREE.BufferAttribute(a,b)};THREE.Int16Attribute=function(a,b){THREE.warn(\"THREE.Int16Attribute has been removed. Use THREE.BufferAttribute( array, itemSize ) instead.\");return new THREE.BufferAttribute(a,b)};"
                << std::endl ;
            out
                << "THREE.Uint16Attribute=function(a,b){THREE.warn(\"THREE.Uint16Attribute has been removed. Use THREE.BufferAttribute( array, itemSize ) instead.\");return new THREE.BufferAttribute(a,b)};THREE.Int32Attribute=function(a,b){THREE.warn(\"THREE.Int32Attribute has been removed. Use THREE.BufferAttribute( array, itemSize ) instead.\");return new THREE.BufferAttribute(a,b)};"
                << std::endl ;
            out
                << "THREE.Uint32Attribute=function(a,b){THREE.warn(\"THREE.Uint32Attribute has been removed. Use THREE.BufferAttribute( array, itemSize ) instead.\");return new THREE.BufferAttribute(a,b)};THREE.Float32Attribute=function(a,b){THREE.warn(\"THREE.Float32Attribute has been removed. Use THREE.BufferAttribute( array, itemSize ) instead.\");return new THREE.BufferAttribute(a,b)};"
                << std::endl ;
            out
                << "THREE.Float64Attribute=function(a,b){THREE.warn(\"THREE.Float64Attribute has been removed. Use THREE.BufferAttribute( array, itemSize ) instead.\");return new THREE.BufferAttribute(a,b)};THREE.DynamicBufferAttribute=function(a,b){THREE.BufferAttribute.call(this,a,b);this.updateRange={offset:0,count:-1}};THREE.DynamicBufferAttribute.prototype=Object.create(THREE.BufferAttribute.prototype);THREE.DynamicBufferAttribute.prototype.constructor=THREE.DynamicBufferAttribute;"
                << std::endl ;
            out
                << "THREE.DynamicBufferAttribute.prototype.clone=function(){return new THREE.DynamicBufferAttribute(new this.array.constructor(this.array),this.itemSize)};THREE.BufferGeometry=function(){Object.defineProperty(this,\"id\",{value:THREE.GeometryIdCount++});this.uuid=THREE.Math.generateUUID();this.name=\"\";this.type=\"BufferGeometry\";this.attributes={};this.attributesKeys=[];this.offsets=this.drawcalls=[];this.boundingSphere=this.boundingBox=null};"
                << std::endl ;
            out
                << "THREE.BufferGeometry.prototype={constructor:THREE.BufferGeometry,addAttribute:function(a,b,c){!1===b instanceof THREE.BufferAttribute?(THREE.warn(\"THREE.BufferGeometry: .addAttribute() now expects ( name, attribute ).\"),this.attributes[a]={array:b,itemSize:c}):(this.attributes[a]=b,this.attributesKeys=Object.keys(this.attributes))},getAttribute:function(a){return this.attributes[a]},addDrawCall:function(a,b,c){this.drawcalls.push({start:a,count:b,index:void 0!==c?c:0})},applyMatrix:function(a){var b="
                << std::endl ;
            out
                << "this.attributes.position;void 0!==b&&(a.applyToVector3Array(b.array),b.needsUpdate=!0);b=this.attributes.normal;void 0!==b&&((new THREE.Matrix3).getNormalMatrix(a).applyToVector3Array(b.array),b.needsUpdate=!0);null!==this.boundingBox&&this.computeBoundingBox();null!==this.boundingSphere&&this.computeBoundingSphere()},center:function(){this.computeBoundingBox();var a=this.boundingBox.center().negate();this.applyMatrix((new THREE.Matrix4).setPosition(a));return a},fromGeometry:function(a,b){b=b||{vertexColors:THREE.NoColors};"
                << std::endl ;
            out
                << "var c=a.vertices,d=a.faces,e=a.faceVertexUvs,f=b.vertexColors,g=0<e[0].length,h=3==d[0].vertexNormals.length,k=new Float32Array(9*d.length);this.addAttribute(\"position\",new THREE.BufferAttribute(k,3));var l=new Float32Array(9*d.length);this.addAttribute(\"normal\",new THREE.BufferAttribute(l,3));if(f!==THREE.NoColors){var p=new Float32Array(9*d.length);this.addAttribute(\"color\",new THREE.BufferAttribute(p,3))}if(!0===g){var q=new Float32Array(6*d.length);this.addAttribute(\"uv\",new THREE.BufferAttribute(q,"
                << std::endl ;
            out
                << "2))}for(var n=0,t=0,r=0;n<d.length;n++,t+=6,r+=9){var s=d[n],u=c[s.a],v=c[s.b],x=c[s.c];k[r]=u.x;k[r+1]=u.y;k[r+2]=u.z;k[r+3]=v.x;k[r+4]=v.y;k[r+5]=v.z;k[r+6]=x.x;k[r+7]=x.y;k[r+8]=x.z;!0===h?(u=s.vertexNormals[0],v=s.vertexNormals[1],x=s.vertexNormals[2],l[r]=u.x,l[r+1]=u.y,l[r+2]=u.z,l[r+3]=v.x,l[r+4]=v.y,l[r+5]=v.z,l[r+6]=x.x,l[r+7]=x.y,l[r+8]=x.z):(u=s.normal,l[r]=u.x,l[r+1]=u.y,l[r+2]=u.z,l[r+3]=u.x,l[r+4]=u.y,l[r+5]=u.z,l[r+6]=u.x,l[r+7]=u.y,l[r+8]=u.z);f===THREE.FaceColors?(s=s.color,p[r]="
                << std::endl ;
            out
                << "s.r,p[r+1]=s.g,p[r+2]=s.b,p[r+3]=s.r,p[r+4]=s.g,p[r+5]=s.b,p[r+6]=s.r,p[r+7]=s.g,p[r+8]=s.b):f===THREE.VertexColors&&(u=s.vertexColors[0],v=s.vertexColors[1],s=s.vertexColors[2],p[r]=u.r,p[r+1]=u.g,p[r+2]=u.b,p[r+3]=v.r,p[r+4]=v.g,p[r+5]=v.b,p[r+6]=s.r,p[r+7]=s.g,p[r+8]=s.b);!0===g&&(s=e[0][n][0],u=e[0][n][1],v=e[0][n][2],q[t]=s.x,q[t+1]=s.y,q[t+2]=u.x,q[t+3]=u.y,q[t+4]=v.x,q[t+5]=v.y)}this.computeBoundingSphere();return this},computeBoundingBox:function(){var a=new THREE.Vector3;return function(){null==="
                << std::endl ;
            out
                << "this.boundingBox&&(this.boundingBox=new THREE.Box3);var b=this.attributes.position.array;if(b){var c=this.boundingBox;c.makeEmpty();for(var d=0,e=b.length;d<e;d+=3)a.set(b[d],b[d+1],b[d+2]),c.expandByPoint(a)}if(void 0===b||0===b.length)this.boundingBox.min.set(0,0,0),this.boundingBox.max.set(0,0,0);(isNaN(this.boundingBox.min.x)||isNaN(this.boundingBox.min.y)||isNaN(this.boundingBox.min.z))&&THREE.error('THREE.BufferGeometry.computeBoundingBox: Computed min/max have NaN values. The \"position\" attribute is likely to have NaN values.')}}(),"
                << std::endl ;
            out
                << "computeBoundingSphere:function(){var a=new THREE.Box3,b=new THREE.Vector3;return function(){null===this.boundingSphere&&(this.boundingSphere=new THREE.Sphere);var c=this.attributes.position.array;if(c){a.makeEmpty();for(var d=this.boundingSphere.center,e=0,f=c.length;e<f;e+=3)b.set(c[e],c[e+1],c[e+2]),a.expandByPoint(b);a.center(d);for(var g=0,e=0,f=c.length;e<f;e+=3)b.set(c[e],c[e+1],c[e+2]),g=Math.max(g,d.distanceToSquared(b));this.boundingSphere.radius=Math.sqrt(g);isNaN(this.boundingSphere.radius)&&"
                << std::endl ;
            out
                << "THREE.error('THREE.BufferGeometry.computeBoundingSphere(): Computed radius is NaN. The \"position\" attribute is likely to have NaN values.')}}}(),computeFaceNormals:function(){},computeVertexNormals:function(){var a=this.attributes;if(a.position){var b=a.position.array;if(void 0===a.normal)this.addAttribute(\"normal\",new THREE.BufferAttribute(new Float32Array(b.length),3));else for(var c=a.normal.array,d=0,e=c.length;d<e;d++)c[d]=0;var c=a.normal.array,f,g,h,k=new THREE.Vector3,l=new THREE.Vector3,"
                << std::endl ;
            out
                << "p=new THREE.Vector3,q=new THREE.Vector3,n=new THREE.Vector3;if(a.index)for(var t=a.index.array,r=0<this.offsets.length?this.offsets:[{start:0,count:t.length,index:0}],s=0,u=r.length;s<u;++s){e=r[s].start;f=r[s].count;for(var v=r[s].index,d=e,e=e+f;d<e;d+=3)f=3*(v+t[d]),g=3*(v+t[d+1]),h=3*(v+t[d+2]),k.fromArray(b,f),l.fromArray(b,g),p.fromArray(b,h),q.subVectors(p,l),n.subVectors(k,l),q.cross(n),c[f]+=q.x,c[f+1]+=q.y,c[f+2]+=q.z,c[g]+=q.x,c[g+1]+=q.y,c[g+2]+=q.z,c[h]+=q.x,c[h+1]+=q.y,c[h+2]+=q.z}else for(d="
                << std::endl ;
            out
                << "0,e=b.length;d<e;d+=9)k.fromArray(b,d),l.fromArray(b,d+3),p.fromArray(b,d+6),q.subVectors(p,l),n.subVectors(k,l),q.cross(n),c[d]=q.x,c[d+1]=q.y,c[d+2]=q.z,c[d+3]=q.x,c[d+4]=q.y,c[d+5]=q.z,c[d+6]=q.x,c[d+7]=q.y,c[d+8]=q.z;this.normalizeNormals();a.normal.needsUpdate=!0}},computeTangents:function(){function a(a,b,c){q.fromArray(d,3*a);n.fromArray(d,3*b);t.fromArray(d,3*c);r.fromArray(f,2*a);s.fromArray(f,2*b);u.fromArray(f,2*c);v=n.x-q.x;x=t.x-q.x;D=n.y-q.y;w=t.y-q.y;y=n.z-q.z;A=t.z-q.z;E=s.x-r.x;G="
                << std::endl ;
            out
                << "u.x-r.x;F=s.y-r.y;z=u.y-r.y;I=1/(E*z-G*F);U.set((z*v-F*x)*I,(z*D-F*w)*I,(z*y-F*A)*I);M.set((E*x-G*v)*I,(E*w-G*D)*I,(E*A-G*y)*I);k[a].add(U);k[b].add(U);k[c].add(U);l[a].add(M);l[b].add(M);l[c].add(M)}function b(a){ha.fromArray(e,3*a);O.copy(ha);ba=k[a];oa.copy(ba);oa.sub(ha.multiplyScalar(ha.dot(ba))).normalize();ja.crossVectors(O,ba);qa=ja.dot(l[a]);ca=0>qa?-1:1;h[4*a]=oa.x;h[4*a+1]=oa.y;h[4*a+2]=oa.z;h[4*a+3]=ca}if(void 0===this.attributes.index||void 0===this.attributes.position||void 0===this.attributes.normal||"
                << std::endl ;
            out
                << "void 0===this.attributes.uv)THREE.warn(\"THREE.BufferGeometry: Missing required attributes (index, position, normal or uv) in BufferGeometry.computeTangents()\");else{var c=this.attributes.index.array,d=this.attributes.position.array,e=this.attributes.normal.array,f=this.attributes.uv.array,g=d.length/3;void 0===this.attributes.tangent&&this.addAttribute(\"tangent\",new THREE.BufferAttribute(new Float32Array(4*g),4));for(var h=this.attributes.tangent.array,k=[],l=[],p=0;p<g;p++)k[p]=new THREE.Vector3,"
                << std::endl ;
            out
                << "l[p]=new THREE.Vector3;var q=new THREE.Vector3,n=new THREE.Vector3,t=new THREE.Vector3,r=new THREE.Vector2,s=new THREE.Vector2,u=new THREE.Vector2,v,x,D,w,y,A,E,G,F,z,I,U=new THREE.Vector3,M=new THREE.Vector3,H,L,P,N,R;0===this.drawcalls.length&&this.addDrawCall(0,c.length,0);var V=this.drawcalls,p=0;for(L=V.length;p<L;++p){H=V[p].start;P=V[p].count;var J=V[p].index,g=H;for(H+=P;g<H;g+=3)P=J+c[g],N=J+c[g+1],R=J+c[g+2],a(P,N,R)}var oa=new THREE.Vector3,ja=new THREE.Vector3,ha=new THREE.Vector3,O=new THREE.Vector3,"
                << std::endl ;
            out
                << "ca,ba,qa,p=0;for(L=V.length;p<L;++p)for(H=V[p].start,P=V[p].count,J=V[p].index,g=H,H+=P;g<H;g+=3)P=J+c[g],N=J+c[g+1],R=J+c[g+2],b(P),b(N),b(R)}},computeOffsets:function(a){void 0===a&&(a=65535);for(var b=this.attributes.index.array,c=this.attributes.position.array,d=b.length/3,e=new Uint16Array(b.length),f=0,g=0,h=[{start:0,count:0,index:0}],k=h[0],l=0,p=0,q=new Int32Array(6),n=new Int32Array(c.length),t=new Int32Array(c.length),r=0;r<c.length;r++)n[r]=-1,t[r]=-1;for(c=0;c<d;c++){for(var s=p=0;3>"
                << std::endl ;
            out
                << "s;s++)r=b[3*c+s],-1==n[r]?(q[2*s]=r,q[2*s+1]=-1,p++):n[r]<k.index?(q[2*s]=r,q[2*s+1]=-1,l++):(q[2*s]=r,q[2*s+1]=n[r]);if(g+p>k.index+a)for(k={start:f,count:0,index:g},h.push(k),p=0;6>p;p+=2)s=q[p+1],-1<s&&s<k.index&&(q[p+1]=-1);for(p=0;6>p;p+=2)r=q[p],s=q[p+1],-1===s&&(s=g++),n[r]=s,t[s]=r,e[f++]=s-k.index,k.count++}this.reorderBuffers(e,t,g);return this.drawcalls=this.offsets=h},merge:function(a,b){if(!1===a instanceof THREE.BufferGeometry)THREE.error(\"THREE.BufferGeometry.merge(): geometry not an instance of THREE.BufferGeometry.\","
                << std::endl ;
            out
                << "a);else{void 0===b&&(b=0);var c=this.attributes,d;for(d in c)if(void 0!==a.attributes[d])for(var e=c[d].array,f=a.attributes[d],g=f.array,h=0,f=f.itemSize*b;h<g.length;h++,f++)e[f]=g[h];return this}},normalizeNormals:function(){for(var a=this.attributes.normal.array,b,c,d,e=0,f=a.length;e<f;e+=3)b=a[e],c=a[e+1],d=a[e+2],b=1/Math.sqrt(b*b+c*c+d*d),a[e]*=b,a[e+1]*=b,a[e+2]*=b},reorderBuffers:function(a,b,c){var d={},e;for(e in this.attributes)\"index\"!=e&&(d[e]=new this.attributes[e].array.constructor(this.attributes[e].itemSize*"
                << std::endl ;
            out
                << "c));for(var f=0;f<c;f++){var g=b[f];for(e in this.attributes)if(\"index\"!=e)for(var h=this.attributes[e].array,k=this.attributes[e].itemSize,l=d[e],p=0;p<k;p++)l[f*k+p]=h[g*k+p]}this.attributes.index.array=a;for(e in this.attributes)\"index\"!=e&&(this.attributes[e].array=d[e],this.attributes[e].numItems=this.attributes[e].itemSize*c)},toJSON:function(){var a={metadata:{version:4,type:\"BufferGeometry\",generator:\"BufferGeometryExporter\"},uuid:this.uuid,type:this.type,data:{attributes:{}}},b=this.attributes,"
                << std::endl ;
            out
                << "c=this.offsets,d=this.boundingSphere,e;for(e in b){var f=b[e],g=Array.prototype.slice.call(f.array);a.data.attributes[e]={itemSize:f.itemSize,type:f.array.constructor.name,array:g}}0<c.length&&(a.data.offsets=JSON.parse(JSON.stringify(c)));null!==d&&(a.data.boundingSphere={center:d.center.toArray(),radius:d.radius});return a},clone:function(){var a=new THREE.BufferGeometry,b;for(b in this.attributes)a.addAttribute(b,this.attributes[b].clone());b=0;for(var c=this.offsets.length;b<c;b++){var d=this.offsets[b];"
                << std::endl ;
            out
                << "a.offsets.push({start:d.start,index:d.index,count:d.count})}return a},dispose:function(){this.dispatchEvent({type:\"dispose\"})}};THREE.EventDispatcher.prototype.apply(THREE.BufferGeometry.prototype);"
                << std::endl ;
            out
                << "THREE.Geometry=function(){Object.defineProperty(this,\"id\",{value:THREE.GeometryIdCount++});this.uuid=THREE.Math.generateUUID();this.name=\"\";this.type=\"Geometry\";this.vertices=[];this.colors=[];this.faces=[];this.faceVertexUvs=[[]];this.morphTargets=[];this.morphColors=[];this.morphNormals=[];this.skinWeights=[];this.skinIndices=[];this.lineDistances=[];this.boundingSphere=this.boundingBox=null;this.hasTangents=!1;this.dynamic=!0;this.groupsNeedUpdate=this.lineDistancesNeedUpdate=this.colorsNeedUpdate="
                << std::endl ;
            out
                << "this.tangentsNeedUpdate=this.normalsNeedUpdate=this.uvsNeedUpdate=this.elementsNeedUpdate=this.verticesNeedUpdate=!1};"
                << std::endl ;
            out
                << "THREE.Geometry.prototype={constructor:THREE.Geometry,applyMatrix:function(a){for(var b=(new THREE.Matrix3).getNormalMatrix(a),c=0,d=this.vertices.length;c<d;c++)this.vertices[c].applyMatrix4(a);c=0;for(d=this.faces.length;c<d;c++){a=this.faces[c];a.normal.applyMatrix3(b).normalize();for(var e=0,f=a.vertexNormals.length;e<f;e++)a.vertexNormals[e].applyMatrix3(b).normalize()}null!==this.boundingBox&&this.computeBoundingBox();null!==this.boundingSphere&&this.computeBoundingSphere();this.normalsNeedUpdate="
                << std::endl ;
            out
                << "this.verticesNeedUpdate=!0},fromBufferGeometry:function(a){for(var b=this,c=a.attributes,d=c.position.array,e=void 0!==c.index?c.index.array:void 0,f=void 0!==c.normal?c.normal.array:void 0,g=void 0!==c.color?c.color.array:void 0,h=void 0!==c.uv?c.uv.array:void 0,k=[],l=[],p=c=0;c<d.length;c+=3,p+=2)b.vertices.push(new THREE.Vector3(d[c],d[c+1],d[c+2])),void 0!==f&&k.push(new THREE.Vector3(f[c],f[c+1],f[c+2])),void 0!==g&&b.colors.push(new THREE.Color(g[c],g[c+1],g[c+2])),void 0!==h&&l.push(new THREE.Vector2(h[p],"
                << std::endl ;
            out
                << "h[p+1]));var q=function(a,c,d){var e=void 0!==f?[k[a].clone(),k[c].clone(),k[d].clone()]:[],n=void 0!==g?[b.colors[a].clone(),b.colors[c].clone(),b.colors[d].clone()]:[];b.faces.push(new THREE.Face3(a,c,d,e,n));void 0!==h&&b.faceVertexUvs[0].push([l[a].clone(),l[c].clone(),l[d].clone()])};if(void 0!==e)if(d=a.drawcalls,0<d.length)for(c=0;c<d.length;c++)for(var p=d[c],n=p.start,t=p.count,r=p.index,p=n,n=n+t;p<n;p+=3)q(r+e[p],r+e[p+1],r+e[p+2]);else for(c=0;c<e.length;c+=3)q(e[c],e[c+1],e[c+2]);else for(c="
                << std::endl ;
            out
                << "0;c<d.length/3;c+=3)q(c,c+1,c+2);this.computeFaceNormals();null!==a.boundingBox&&(this.boundingBox=a.boundingBox.clone());null!==a.boundingSphere&&(this.boundingSphere=a.boundingSphere.clone());return this},center:function(){this.computeBoundingBox();var a=this.boundingBox.center().negate();this.applyMatrix((new THREE.Matrix4).setPosition(a));return a},computeFaceNormals:function(){for(var a=new THREE.Vector3,b=new THREE.Vector3,c=0,d=this.faces.length;c<d;c++){var e=this.faces[c],f=this.vertices[e.a],"
                << std::endl ;
            out
                << "g=this.vertices[e.b];a.subVectors(this.vertices[e.c],g);b.subVectors(f,g);a.cross(b);a.normalize();e.normal.copy(a)}},computeVertexNormals:function(a){var b,c,d;d=Array(this.vertices.length);b=0;for(c=this.vertices.length;b<c;b++)d[b]=new THREE.Vector3;if(a){var e,f,g,h=new THREE.Vector3,k=new THREE.Vector3;a=0;for(b=this.faces.length;a<b;a++)c=this.faces[a],e=this.vertices[c.a],f=this.vertices[c.b],g=this.vertices[c.c],h.subVectors(g,f),k.subVectors(e,f),h.cross(k),d[c.a].add(h),d[c.b].add(h),d[c.c].add(h)}else for(a="
                << std::endl ;
            out
                << "0,b=this.faces.length;a<b;a++)c=this.faces[a],d[c.a].add(c.normal),d[c.b].add(c.normal),d[c.c].add(c.normal);b=0;for(c=this.vertices.length;b<c;b++)d[b].normalize();a=0;for(b=this.faces.length;a<b;a++)c=this.faces[a],c.vertexNormals[0]=d[c.a].clone(),c.vertexNormals[1]=d[c.b].clone(),c.vertexNormals[2]=d[c.c].clone()},computeMorphNormals:function(){var a,b,c,d,e;c=0;for(d=this.faces.length;c<d;c++)for(e=this.faces[c],e.__originalFaceNormal?e.__originalFaceNormal.copy(e.normal):e.__originalFaceNormal="
                << std::endl ;
            out
                << "e.normal.clone(),e.__originalVertexNormals||(e.__originalVertexNormals=[]),a=0,b=e.vertexNormals.length;a<b;a++)e.__originalVertexNormals[a]?e.__originalVertexNormals[a].copy(e.vertexNormals[a]):e.__originalVertexNormals[a]=e.vertexNormals[a].clone();var f=new THREE.Geometry;f.faces=this.faces;a=0;for(b=this.morphTargets.length;a<b;a++){if(!this.morphNormals[a]){this.morphNormals[a]={};this.morphNormals[a].faceNormals=[];this.morphNormals[a].vertexNormals=[];e=this.morphNormals[a].faceNormals;var g="
                << std::endl ;
            out
                << "this.morphNormals[a].vertexNormals,h,k;c=0;for(d=this.faces.length;c<d;c++)h=new THREE.Vector3,k={a:new THREE.Vector3,b:new THREE.Vector3,c:new THREE.Vector3},e.push(h),g.push(k)}g=this.morphNormals[a];f.vertices=this.morphTargets[a].vertices;f.computeFaceNormals();f.computeVertexNormals();c=0;for(d=this.faces.length;c<d;c++)e=this.faces[c],h=g.faceNormals[c],k=g.vertexNormals[c],h.copy(e.normal),k.a.copy(e.vertexNormals[0]),k.b.copy(e.vertexNormals[1]),k.c.copy(e.vertexNormals[2])}c=0;for(d=this.faces.length;c<"
                << std::endl ;
            out
                << "d;c++)e=this.faces[c],e.normal=e.__originalFaceNormal,e.vertexNormals=e.__originalVertexNormals},computeTangents:function(){var a,b,c,d,e,f,g,h,k,l,p,q,n,t,r,s,u,v=[],x=[];c=new THREE.Vector3;var D=new THREE.Vector3,w=new THREE.Vector3,y=new THREE.Vector3,A=new THREE.Vector3;a=0;for(b=this.vertices.length;a<b;a++)v[a]=new THREE.Vector3,x[a]=new THREE.Vector3;a=0;for(b=this.faces.length;a<b;a++)e=this.faces[a],f=this.faceVertexUvs[0][a],d=e.a,u=e.b,e=e.c,g=this.vertices[d],h=this.vertices[u],k=this.vertices[e],"
                << std::endl ;
            out
                << "l=f[0],p=f[1],q=f[2],f=h.x-g.x,n=k.x-g.x,t=h.y-g.y,r=k.y-g.y,h=h.z-g.z,g=k.z-g.z,k=p.x-l.x,s=q.x-l.x,p=p.y-l.y,l=q.y-l.y,q=1/(k*l-s*p),c.set((l*f-p*n)*q,(l*t-p*r)*q,(l*h-p*g)*q),D.set((k*n-s*f)*q,(k*r-s*t)*q,(k*g-s*h)*q),v[d].add(c),v[u].add(c),v[e].add(c),x[d].add(D),x[u].add(D),x[e].add(D);D=[\"a\",\"b\",\"c\",\"d\"];a=0;for(b=this.faces.length;a<b;a++)for(e=this.faces[a],c=0;c<Math.min(e.vertexNormals.length,3);c++)A.copy(e.vertexNormals[c]),d=e[D[c]],u=v[d],w.copy(u),w.sub(A.multiplyScalar(A.dot(u))).normalize(),"
                << std::endl ;
            out
                << "y.crossVectors(e.vertexNormals[c],u),d=y.dot(x[d]),d=0>d?-1:1,e.vertexTangents[c]=new THREE.Vector4(w.x,w.y,w.z,d);this.hasTangents=!0},computeLineDistances:function(){for(var a=0,b=this.vertices,c=0,d=b.length;c<d;c++)0<c&&(a+=b[c].distanceTo(b[c-1])),this.lineDistances[c]=a},computeBoundingBox:function(){null===this.boundingBox&&(this.boundingBox=new THREE.Box3);this.boundingBox.setFromPoints(this.vertices)},computeBoundingSphere:function(){null===this.boundingSphere&&(this.boundingSphere=new THREE.Sphere);"
                << std::endl ;
            out
                << "this.boundingSphere.setFromPoints(this.vertices)},merge:function(a,b,c){if(!1===a instanceof THREE.Geometry)THREE.error(\"THREE.Geometry.merge(): geometry not an instance of THREE.Geometry.\",a);else{var d,e=this.vertices.length,f=this.vertices,g=a.vertices,h=this.faces,k=a.faces,l=this.faceVertexUvs[0];a=a.faceVertexUvs[0];void 0===c&&(c=0);void 0!==b&&(d=(new THREE.Matrix3).getNormalMatrix(b));for(var p=0,q=g.length;p<q;p++){var n=g[p].clone();void 0!==b&&n.applyMatrix4(b);f.push(n)}p=0;for(q=k.length;p<"
                << std::endl ;
            out
                << "q;p++){var g=k[p],t,r=g.vertexNormals,s=g.vertexColors,n=new THREE.Face3(g.a+e,g.b+e,g.c+e);n.normal.copy(g.normal);void 0!==d&&n.normal.applyMatrix3(d).normalize();b=0;for(f=r.length;b<f;b++)t=r[b].clone(),void 0!==d&&t.applyMatrix3(d).normalize(),n.vertexNormals.push(t);n.color.copy(g.color);b=0;for(f=s.length;b<f;b++)t=s[b],n.vertexColors.push(t.clone());n.materialIndex=g.materialIndex+c;h.push(n)}p=0;for(q=a.length;p<q;p++)if(c=a[p],d=[],void 0!==c){b=0;for(f=c.length;b<f;b++)d.push(c[b].clone());"
                << std::endl ;
            out
                << "l.push(d)}}},mergeMesh:function(a){!1===a instanceof THREE.Mesh?THREE.error(\"THREE.Geometry.mergeMesh(): mesh not an instance of THREE.Mesh.\",a):(a.matrixAutoUpdate&&a.updateMatrix(),this.merge(a.geometry,a.matrix))},mergeVertices:function(){var a={},b=[],c=[],d,e=Math.pow(10,4),f,g;f=0;for(g=this.vertices.length;f<g;f++)d=this.vertices[f],d=Math.round(d.x*e)+\"_\"+Math.round(d.y*e)+\"_\"+Math.round(d.z*e),void 0===a[d]?(a[d]=f,b.push(this.vertices[f]),c[f]=b.length-1):c[f]=c[a[d]];a=[];f=0;for(g=this.faces.length;f<"
                << std::endl ;
            out
                << "g;f++)for(e=this.faces[f],e.a=c[e.a],e.b=c[e.b],e.c=c[e.c],e=[e.a,e.b,e.c],d=0;3>d;d++)if(e[d]==e[(d+1)%3]){a.push(f);break}for(f=a.length-1;0<=f;f--)for(e=a[f],this.faces.splice(e,1),c=0,g=this.faceVertexUvs.length;c<g;c++)this.faceVertexUvs[c].splice(e,1);f=this.vertices.length-b.length;this.vertices=b;return f},toJSON:function(){function a(a,b,c){return c?a|1<<b:a&~(1<<b)}function b(a){var b=a.x.toString()+a.y.toString()+a.z.toString();if(void 0!==l[b])return l[b];l[b]=k.length/3;k.push(a.x,a.y,"
                << std::endl ;
            out
                << "a.z);return l[b]}function c(a){var b=a.r.toString()+a.g.toString()+a.b.toString();if(void 0!==q[b])return q[b];q[b]=p.length;p.push(a.getHex());return q[b]}function d(a){var b=a.x.toString()+a.y.toString();if(void 0!==t[b])return t[b];t[b]=n.length/2;n.push(a.x,a.y);return t[b]}var e={metadata:{version:4,type:\"BufferGeometry\",generator:\"BufferGeometryExporter\"},uuid:this.uuid,type:this.type};\"\"!==this.name&&(e.name=this.name);if(void 0!==this.parameters){var f=this.parameters,g;for(g in f)void 0!=="
                << std::endl ;
            out
                << "f[g]&&(e[g]=f[g]);return e}f=[];for(g=0;g<this.vertices.length;g++){var h=this.vertices[g];f.push(h.x,h.y,h.z)}var h=[],k=[],l={},p=[],q={},n=[],t={};for(g=0;g<this.faces.length;g++){var r=this.faces[g],s=void 0!==this.faceVertexUvs[0][g],u=0<r.normal.length(),v=0<r.vertexNormals.length,x=1!==r.color.r||1!==r.color.g||1!==r.color.b,D=0<r.vertexColors.length,w=0,w=a(w,0,0),w=a(w,1,!1),w=a(w,2,!1),w=a(w,3,s),w=a(w,4,u),w=a(w,5,v),w=a(w,6,x),w=a(w,7,D);h.push(w);h.push(r.a,r.b,r.c);s&&(s=this.faceVertexUvs[0][g],"
                << std::endl ;
            out
                << "h.push(d(s[0]),d(s[1]),d(s[2])));u&&h.push(b(r.normal));v&&(u=r.vertexNormals,h.push(b(u[0]),b(u[1]),b(u[2])));x&&h.push(c(r.color));D&&(r=r.vertexColors,h.push(c(r[0]),c(r[1]),c(r[2])))}e.data={};e.data.vertices=f;e.data.normals=k;0<p.length&&(e.data.colors=p);0<n.length&&(e.data.uvs=[n]);e.data.faces=h;return e},clone:function(){for(var a=new THREE.Geometry,b=this.vertices,c=0,d=b.length;c<d;c++)a.vertices.push(b[c].clone());b=this.faces;c=0;for(d=b.length;c<d;c++)a.faces.push(b[c].clone());c=0;"
                << std::endl ;
            out
                << "for(d=this.faceVertexUvs.length;c<d;c++){b=this.faceVertexUvs[c];void 0===a.faceVertexUvs[c]&&(a.faceVertexUvs[c]=[]);for(var e=0,f=b.length;e<f;e++){for(var g=b[e],h=[],k=0,l=g.length;k<l;k++)h.push(g[k].clone());a.faceVertexUvs[c].push(h)}}return a},dispose:function(){this.dispatchEvent({type:\"dispose\"})}};THREE.EventDispatcher.prototype.apply(THREE.Geometry.prototype);THREE.GeometryIdCount=0;"
                << std::endl ;
            out
                << "THREE.Camera=function(){THREE.Object3D.call(this);this.type=\"Camera\";this.matrixWorldInverse=new THREE.Matrix4;this.projectionMatrix=new THREE.Matrix4};THREE.Camera.prototype=Object.create(THREE.Object3D.prototype);THREE.Camera.prototype.constructor=THREE.Camera;THREE.Camera.prototype.getWorldDirection=function(){var a=new THREE.Quaternion;return function(b){b=b||new THREE.Vector3;this.getWorldQuaternion(a);return b.set(0,0,-1).applyQuaternion(a)}}();"
                << std::endl ;
            out
                << "THREE.Camera.prototype.lookAt=function(){var a=new THREE.Matrix4;return function(b){a.lookAt(this.position,b,this.up);this.quaternion.setFromRotationMatrix(a)}}();THREE.Camera.prototype.clone=function(a){void 0===a&&(a=new THREE.Camera);THREE.Object3D.prototype.clone.call(this,a);a.matrixWorldInverse.copy(this.matrixWorldInverse);a.projectionMatrix.copy(this.projectionMatrix);return a};"
                << std::endl ;
            out
                << "THREE.CubeCamera=function(a,b,c){THREE.Object3D.call(this);this.type=\"CubeCamera\";var d=new THREE.PerspectiveCamera(90,1,a,b);d.up.set(0,-1,0);d.lookAt(new THREE.Vector3(1,0,0));this.add(d);var e=new THREE.PerspectiveCamera(90,1,a,b);e.up.set(0,-1,0);e.lookAt(new THREE.Vector3(-1,0,0));this.add(e);var f=new THREE.PerspectiveCamera(90,1,a,b);f.up.set(0,0,1);f.lookAt(new THREE.Vector3(0,1,0));this.add(f);var g=new THREE.PerspectiveCamera(90,1,a,b);g.up.set(0,0,-1);g.lookAt(new THREE.Vector3(0,-1,0));"
                << std::endl ;
            out
                << "this.add(g);var h=new THREE.PerspectiveCamera(90,1,a,b);h.up.set(0,-1,0);h.lookAt(new THREE.Vector3(0,0,1));this.add(h);var k=new THREE.PerspectiveCamera(90,1,a,b);k.up.set(0,-1,0);k.lookAt(new THREE.Vector3(0,0,-1));this.add(k);this.renderTarget=new THREE.WebGLRenderTargetCube(c,c,{format:THREE.RGBFormat,magFilter:THREE.LinearFilter,minFilter:THREE.LinearFilter});this.updateCubeMap=function(a,b){var c=this.renderTarget,n=c.generateMipmaps;c.generateMipmaps=!1;c.activeCubeFace=0;a.render(b,d,c);c.activeCubeFace="
                << std::endl ;
            out
                << "1;a.render(b,e,c);c.activeCubeFace=2;a.render(b,f,c);c.activeCubeFace=3;a.render(b,g,c);c.activeCubeFace=4;a.render(b,h,c);c.generateMipmaps=n;c.activeCubeFace=5;a.render(b,k,c)}};THREE.CubeCamera.prototype=Object.create(THREE.Object3D.prototype);THREE.CubeCamera.prototype.constructor=THREE.CubeCamera;"
                << std::endl ;
            out
                << "THREE.OrthographicCamera=function(a,b,c,d,e,f){THREE.Camera.call(this);this.type=\"OrthographicCamera\";this.zoom=1;this.left=a;this.right=b;this.top=c;this.bottom=d;this.near=void 0!==e?e:.1;this.far=void 0!==f?f:2E3;this.updateProjectionMatrix()};THREE.OrthographicCamera.prototype=Object.create(THREE.Camera.prototype);THREE.OrthographicCamera.prototype.constructor=THREE.OrthographicCamera;"
                << std::endl ;
            out
                << "THREE.OrthographicCamera.prototype.updateProjectionMatrix=function(){var a=(this.right-this.left)/(2*this.zoom),b=(this.top-this.bottom)/(2*this.zoom),c=(this.right+this.left)/2,d=(this.top+this.bottom)/2;this.projectionMatrix.makeOrthographic(c-a,c+a,d+b,d-b,this.near,this.far)};"
                << std::endl ;
            out
                << "THREE.OrthographicCamera.prototype.clone=function(){var a=new THREE.OrthographicCamera;THREE.Camera.prototype.clone.call(this,a);a.zoom=this.zoom;a.left=this.left;a.right=this.right;a.top=this.top;a.bottom=this.bottom;a.near=this.near;a.far=this.far;a.projectionMatrix.copy(this.projectionMatrix);return a};"
                << std::endl ;
            out
                << "THREE.PerspectiveCamera=function(a,b,c,d){THREE.Camera.call(this);this.type=\"PerspectiveCamera\";this.zoom=1;this.fov=void 0!==a?a:50;this.aspect=void 0!==b?b:1;this.near=void 0!==c?c:.1;this.far=void 0!==d?d:2E3;this.updateProjectionMatrix()};THREE.PerspectiveCamera.prototype=Object.create(THREE.Camera.prototype);THREE.PerspectiveCamera.prototype.constructor=THREE.PerspectiveCamera;"
                << std::endl ;
            out
                << "THREE.PerspectiveCamera.prototype.setLens=function(a,b){void 0===b&&(b=24);this.fov=2*THREE.Math.radToDeg(Math.atan(b/(2*a)));this.updateProjectionMatrix()};THREE.PerspectiveCamera.prototype.setViewOffset=function(a,b,c,d,e,f){this.fullWidth=a;this.fullHeight=b;this.x=c;this.y=d;this.width=e;this.height=f;this.updateProjectionMatrix()};"
                << std::endl ;
            out
                << "THREE.PerspectiveCamera.prototype.updateProjectionMatrix=function(){var a=THREE.Math.radToDeg(2*Math.atan(Math.tan(.5*THREE.Math.degToRad(this.fov))/this.zoom));if(this.fullWidth){var b=this.fullWidth/this.fullHeight,a=Math.tan(THREE.Math.degToRad(.5*a))*this.near,c=-a,d=b*c,b=Math.abs(b*a-d),c=Math.abs(a-c);this.projectionMatrix.makeFrustum(d+this.x*b/this.fullWidth,d+(this.x+this.width)*b/this.fullWidth,a-(this.y+this.height)*c/this.fullHeight,a-this.y*c/this.fullHeight,this.near,this.far)}else this.projectionMatrix.makePerspective(a,"
                << std::endl ;
            out
                << "this.aspect,this.near,this.far)};THREE.PerspectiveCamera.prototype.clone=function(){var a=new THREE.PerspectiveCamera;THREE.Camera.prototype.clone.call(this,a);a.zoom=this.zoom;a.fov=this.fov;a.aspect=this.aspect;a.near=this.near;a.far=this.far;a.projectionMatrix.copy(this.projectionMatrix);return a};THREE.Light=function(a){THREE.Object3D.call(this);this.type=\"Light\";this.color=new THREE.Color(a)};THREE.Light.prototype=Object.create(THREE.Object3D.prototype);THREE.Light.prototype.constructor=THREE.Light;"
                << std::endl ;
            out
                << "THREE.Light.prototype.clone=function(a){void 0===a&&(a=new THREE.Light);THREE.Object3D.prototype.clone.call(this,a);a.color.copy(this.color);return a};THREE.AmbientLight=function(a){THREE.Light.call(this,a);this.type=\"AmbientLight\"};THREE.AmbientLight.prototype=Object.create(THREE.Light.prototype);THREE.AmbientLight.prototype.constructor=THREE.AmbientLight;THREE.AmbientLight.prototype.clone=function(){var a=new THREE.AmbientLight;THREE.Light.prototype.clone.call(this,a);return a};"
                << std::endl ;
            out
                << "THREE.AreaLight=function(a,b){THREE.Light.call(this,a);this.type=\"AreaLight\";this.normal=new THREE.Vector3(0,-1,0);this.right=new THREE.Vector3(1,0,0);this.intensity=void 0!==b?b:1;this.height=this.width=1;this.constantAttenuation=1.5;this.linearAttenuation=.5;this.quadraticAttenuation=.1};THREE.AreaLight.prototype=Object.create(THREE.Light.prototype);THREE.AreaLight.prototype.constructor=THREE.AreaLight;"
                << std::endl ;
            out
                << "THREE.DirectionalLight=function(a,b){THREE.Light.call(this,a);this.type=\"DirectionalLight\";this.position.set(0,1,0);this.target=new THREE.Object3D;this.intensity=void 0!==b?b:1;this.onlyShadow=this.castShadow=!1;this.shadowCameraNear=50;this.shadowCameraFar=5E3;this.shadowCameraLeft=-500;this.shadowCameraTop=this.shadowCameraRight=500;this.shadowCameraBottom=-500;this.shadowCameraVisible=!1;this.shadowBias=0;this.shadowDarkness=.5;this.shadowMapHeight=this.shadowMapWidth=512;this.shadowCascade=!1;"
                << std::endl ;
            out
                << "this.shadowCascadeOffset=new THREE.Vector3(0,0,-1E3);this.shadowCascadeCount=2;this.shadowCascadeBias=[0,0,0];this.shadowCascadeWidth=[512,512,512];this.shadowCascadeHeight=[512,512,512];this.shadowCascadeNearZ=[-1,.99,.998];this.shadowCascadeFarZ=[.99,.998,1];this.shadowCascadeArray=[];this.shadowMatrix=this.shadowCamera=this.shadowMapSize=this.shadowMap=null};THREE.DirectionalLight.prototype=Object.create(THREE.Light.prototype);THREE.DirectionalLight.prototype.constructor=THREE.DirectionalLight;"
                << std::endl ;
            out
                << "THREE.DirectionalLight.prototype.clone=function(){var a=new THREE.DirectionalLight;THREE.Light.prototype.clone.call(this,a);a.target=this.target.clone();a.intensity=this.intensity;a.castShadow=this.castShadow;a.onlyShadow=this.onlyShadow;a.shadowCameraNear=this.shadowCameraNear;a.shadowCameraFar=this.shadowCameraFar;a.shadowCameraLeft=this.shadowCameraLeft;a.shadowCameraRight=this.shadowCameraRight;a.shadowCameraTop=this.shadowCameraTop;a.shadowCameraBottom=this.shadowCameraBottom;a.shadowCameraVisible="
                << std::endl ;
            out
                << "this.shadowCameraVisible;a.shadowBias=this.shadowBias;a.shadowDarkness=this.shadowDarkness;a.shadowMapWidth=this.shadowMapWidth;a.shadowMapHeight=this.shadowMapHeight;a.shadowCascade=this.shadowCascade;a.shadowCascadeOffset.copy(this.shadowCascadeOffset);a.shadowCascadeCount=this.shadowCascadeCount;a.shadowCascadeBias=this.shadowCascadeBias.slice(0);a.shadowCascadeWidth=this.shadowCascadeWidth.slice(0);a.shadowCascadeHeight=this.shadowCascadeHeight.slice(0);a.shadowCascadeNearZ=this.shadowCascadeNearZ.slice(0);"
                << std::endl ;
            out
                << "a.shadowCascadeFarZ=this.shadowCascadeFarZ.slice(0);return a};THREE.HemisphereLight=function(a,b,c){THREE.Light.call(this,a);this.type=\"HemisphereLight\";this.position.set(0,100,0);this.groundColor=new THREE.Color(b);this.intensity=void 0!==c?c:1};THREE.HemisphereLight.prototype=Object.create(THREE.Light.prototype);THREE.HemisphereLight.prototype.constructor=THREE.HemisphereLight;"
                << std::endl ;
            out
                << "THREE.HemisphereLight.prototype.clone=function(){var a=new THREE.HemisphereLight;THREE.Light.prototype.clone.call(this,a);a.groundColor.copy(this.groundColor);a.intensity=this.intensity;return a};THREE.PointLight=function(a,b,c,d){THREE.Light.call(this,a);this.type=\"PointLight\";this.intensity=void 0!==b?b:1;this.distance=void 0!==c?c:0;this.decay=void 0!==d?d:1};THREE.PointLight.prototype=Object.create(THREE.Light.prototype);THREE.PointLight.prototype.constructor=THREE.PointLight;"
                << std::endl ;
            out
                << "THREE.PointLight.prototype.clone=function(){var a=new THREE.PointLight;THREE.Light.prototype.clone.call(this,a);a.intensity=this.intensity;a.distance=this.distance;a.decay=this.decay;return a};"
                << std::endl ;
            out
                << "THREE.SpotLight=function(a,b,c,d,e,f){THREE.Light.call(this,a);this.type=\"SpotLight\";this.position.set(0,1,0);this.target=new THREE.Object3D;this.intensity=void 0!==b?b:1;this.distance=void 0!==c?c:0;this.angle=void 0!==d?d:Math.PI/3;this.exponent=void 0!==e?e:10;this.decay=void 0!==f?f:1;this.onlyShadow=this.castShadow=!1;this.shadowCameraNear=50;this.shadowCameraFar=5E3;this.shadowCameraFov=50;this.shadowCameraVisible=!1;this.shadowBias=0;this.shadowDarkness=.5;this.shadowMapHeight=this.shadowMapWidth="
                << std::endl ;
            out
                << "512;this.shadowMatrix=this.shadowCamera=this.shadowMapSize=this.shadowMap=null};THREE.SpotLight.prototype=Object.create(THREE.Light.prototype);THREE.SpotLight.prototype.constructor=THREE.SpotLight;"
                << std::endl ;
            out
                << "THREE.SpotLight.prototype.clone=function(){var a=new THREE.SpotLight;THREE.Light.prototype.clone.call(this,a);a.target=this.target.clone();a.intensity=this.intensity;a.distance=this.distance;a.angle=this.angle;a.exponent=this.exponent;a.decay=this.decay;a.castShadow=this.castShadow;a.onlyShadow=this.onlyShadow;a.shadowCameraNear=this.shadowCameraNear;a.shadowCameraFar=this.shadowCameraFar;a.shadowCameraFov=this.shadowCameraFov;a.shadowCameraVisible=this.shadowCameraVisible;a.shadowBias=this.shadowBias;"
                << std::endl ;
            out
                << "a.shadowDarkness=this.shadowDarkness;a.shadowMapWidth=this.shadowMapWidth;a.shadowMapHeight=this.shadowMapHeight;return a};THREE.Cache={files:{},add:function(a,b){this.files[a]=b},get:function(a){return this.files[a]},remove:function(a){delete this.files[a]},clear:function(){this.files={}}};"
                << std::endl ;
            out
                << "THREE.Loader=function(a){this.statusDomElement=(this.showStatus=a)?THREE.Loader.prototype.addStatusElement():null;this.imageLoader=new THREE.ImageLoader;this.onLoadStart=function(){};this.onLoadProgress=function(){};this.onLoadComplete=function(){}};"
                << std::endl ;
            out
                << "THREE.Loader.prototype={constructor:THREE.Loader,crossOrigin:void 0,addStatusElement:function(){var a=document.createElement(\"div\");a.style.position=\"absolute\";a.style.right=\"0px\";a.style.top=\"0px\";a.style.fontSize=\"0.8em\";a.style.textAlign=\"left\";a.style.background=\"rgba(0,0,0,0.25)\";a.style.color=\"#fff\";a.style.width=\"120px\";a.style.padding=\"0.5em 0.5em 0.5em 0.5em\";a.style.zIndex=1E3;a.innerHTML=\"Loading ...\";return a},updateProgress:function(a){var b=\"Loaded \",b=a.total?b+((100*a.loaded/a.total).toFixed(0)+"
                << std::endl ;
            out
                << "\"%\"):b+((a.loaded/1024).toFixed(2)+\" KB\");this.statusDomElement.innerHTML=b},extractUrlBase:function(a){a=a.split(\"/\");if(1===a.length)return\"./\";a.pop();return a.join(\"/\")+\"/\"},initMaterials:function(a,b){for(var c=[],d=0;d<a.length;++d)c[d]=this.createMaterial(a[d],b);return c},needsTangents:function(a){for(var b=0,c=a.length;b<c;b++)if(a[b]instanceof THREE.ShaderMaterial)return!0;return!1},createMaterial:function(a,b){function c(a){a=Math.log(a)/Math.LN2;return Math.pow(2,Math.round(a))}function d(a,"
                << std::endl ;
            out
                << "d,e,g,h,k,s){var u=b+e,v,x=THREE.Loader.Handlers.get(u);null!==x?v=x.load(u):(v=new THREE.Texture,x=f.imageLoader,x.crossOrigin=f.crossOrigin,x.load(u,function(a){if(!1===THREE.Math.isPowerOfTwo(a.width)||!1===THREE.Math.isPowerOfTwo(a.height)){var b=c(a.width),d=c(a.height),e=document.createElement(\"canvas\");e.width=b;e.height=d;e.getContext(\"2d\").drawImage(a,0,0,b,d);v.image=e}else v.image=a;v.needsUpdate=!0}));v.sourceFile=e;g&&(v.repeat.set(g[0],g[1]),1!==g[0]&&(v.wrapS=THREE.RepeatWrapping),"
                << std::endl ;
            out
                << "1!==g[1]&&(v.wrapT=THREE.RepeatWrapping));h&&v.offset.set(h[0],h[1]);k&&(e={repeat:THREE.RepeatWrapping,mirror:THREE.MirroredRepeatWrapping},void 0!==e[k[0]]&&(v.wrapS=e[k[0]]),void 0!==e[k[1]]&&(v.wrapT=e[k[1]]));s&&(v.anisotropy=s);a[d]=v}function e(a){return(255*a[0]<<16)+(255*a[1]<<8)+255*a[2]}var f=this,g=\"MeshLambertMaterial\",h={color:15658734,opacity:1,map:null,lightMap:null,normalMap:null,bumpMap:null,wireframe:!1};if(a.shading){var k=a.shading.toLowerCase();\"phong\"===k?g=\"MeshPhongMaterial\":"
                << std::endl ;
            out
                << "\"basic\"===k&&(g=\"MeshBasicMaterial\")}void 0!==a.blending&&void 0!==THREE[a.blending]&&(h.blending=THREE[a.blending]);void 0!==a.transparent&&(h.transparent=a.transparent);void 0!==a.opacity&&1>a.opacity&&(h.transparent=!0);void 0!==a.depthTest&&(h.depthTest=a.depthTest);void 0!==a.depthWrite&&(h.depthWrite=a.depthWrite);void 0!==a.visible&&(h.visible=a.visible);void 0!==a.flipSided&&(h.side=THREE.BackSide);void 0!==a.doubleSided&&(h.side=THREE.DoubleSide);void 0!==a.wireframe&&(h.wireframe=a.wireframe);"
                << std::endl ;
            out
                << "void 0!==a.vertexColors&&(\"face\"===a.vertexColors?h.vertexColors=THREE.FaceColors:a.vertexColors&&(h.vertexColors=THREE.VertexColors));a.colorDiffuse?h.color=e(a.colorDiffuse):a.DbgColor&&(h.color=a.DbgColor);a.colorSpecular&&(h.specular=e(a.colorSpecular));a.colorEmissive&&(h.emissive=e(a.colorEmissive));void 0!==a.transparency&&(console.warn(\"THREE.Loader: transparency has been renamed to opacity\"),a.opacity=a.transparency);void 0!==a.opacity&&(h.opacity=a.opacity);a.specularCoef&&(h.shininess="
                << std::endl ;
            out
                << "a.specularCoef);a.mapDiffuse&&b&&d(h,\"map\",a.mapDiffuse,a.mapDiffuseRepeat,a.mapDiffuseOffset,a.mapDiffuseWrap,a.mapDiffuseAnisotropy);a.mapLight&&b&&d(h,\"lightMap\",a.mapLight,a.mapLightRepeat,a.mapLightOffset,a.mapLightWrap,a.mapLightAnisotropy);a.mapBump&&b&&d(h,\"bumpMap\",a.mapBump,a.mapBumpRepeat,a.mapBumpOffset,a.mapBumpWrap,a.mapBumpAnisotropy);a.mapNormal&&b&&d(h,\"normalMap\",a.mapNormal,a.mapNormalRepeat,a.mapNormalOffset,a.mapNormalWrap,a.mapNormalAnisotropy);a.mapSpecular&&b&&d(h,\"specularMap\","
                << std::endl ;
            out
                << "a.mapSpecular,a.mapSpecularRepeat,a.mapSpecularOffset,a.mapSpecularWrap,a.mapSpecularAnisotropy);a.mapAlpha&&b&&d(h,\"alphaMap\",a.mapAlpha,a.mapAlphaRepeat,a.mapAlphaOffset,a.mapAlphaWrap,a.mapAlphaAnisotropy);a.mapBumpScale&&(h.bumpScale=a.mapBumpScale);a.mapNormalFactor&&(h.normalScale=new THREE.Vector2(a.mapNormalFactor,a.mapNormalFactor));g=new THREE[g](h);void 0!==a.DbgName&&(g.name=a.DbgName);return g}};"
                << std::endl ;
            out
                << "THREE.Loader.Handlers={handlers:[],add:function(a,b){this.handlers.push(a,b)},get:function(a){for(var b=0,c=this.handlers.length;b<c;b+=2){var d=this.handlers[b+1];if(this.handlers[b].test(a))return d}return null}};THREE.XHRLoader=function(a){this.manager=void 0!==a?a:THREE.DefaultLoadingManager};"
                << std::endl ;
            out
                << "THREE.XHRLoader.prototype={constructor:THREE.XHRLoader,load:function(a,b,c,d){var e=this,f=THREE.Cache.get(a);void 0!==f?b&&b(f):(f=new XMLHttpRequest,f.open(\"GET\",a,!0),f.addEventListener(\"load\",function(c){THREE.Cache.add(a,this.response);b&&b(this.response);e.manager.itemEnd(a)},!1),void 0!==c&&f.addEventListener(\"progress\",function(a){c(a)},!1),void 0!==d&&f.addEventListener(\"error\",function(a){d(a)},!1),void 0!==this.crossOrigin&&(f.crossOrigin=this.crossOrigin),void 0!==this.responseType&&(f.responseType="
                << std::endl ;
            out
                << "this.responseType),f.send(null),e.manager.itemStart(a))},setResponseType:function(a){this.responseType=a},setCrossOrigin:function(a){this.crossOrigin=a}};THREE.ImageLoader=function(a){this.manager=void 0!==a?a:THREE.DefaultLoadingManager};"
                << std::endl ;
            out
                << "THREE.ImageLoader.prototype={constructor:THREE.ImageLoader,load:function(a,b,c,d){var e=this,f=THREE.Cache.get(a);if(void 0!==f)b(f);else return f=document.createElement(\"img\"),f.addEventListener(\"load\",function(c){THREE.Cache.add(a,this);b&&b(this);e.manager.itemEnd(a)},!1),void 0!==c&&f.addEventListener(\"progress\",function(a){c(a)},!1),void 0!==d&&f.addEventListener(\"error\",function(a){d(a)},!1),void 0!==this.crossOrigin&&(f.crossOrigin=this.crossOrigin),f.src=a,e.manager.itemStart(a),f},setCrossOrigin:function(a){this.crossOrigin="
                << std::endl ;
            out
                << "a}};THREE.JSONLoader=function(a){THREE.Loader.call(this,a);this.withCredentials=!1};THREE.JSONLoader.prototype=Object.create(THREE.Loader.prototype);THREE.JSONLoader.prototype.constructor=THREE.JSONLoader;THREE.JSONLoader.prototype.load=function(a,b,c){c=c&&\"string\"===typeof c?c:this.extractUrlBase(a);this.onLoadStart();this.loadAjaxJSON(this,a,b,c)};"
                << std::endl ;
            out
                << "THREE.JSONLoader.prototype.loadAjaxJSON=function(a,b,c,d,e){var f=new XMLHttpRequest,g=0;f.onreadystatechange=function(){if(f.readyState===f.DONE)if(200===f.status||0===f.status){if(f.responseText){var h=JSON.parse(f.responseText),k=h.metadata;if(void 0!==k){if(\"object\"===k.type){THREE.error(\"THREE.JSONLoader: \"+b+\" should be loaded with THREE.ObjectLoader instead.\");return}if(\"scene\"===k.type){THREE.error(\"THREE.JSONLoader: \"+b+\" seems to be a Scene. Use THREE.SceneLoader instead.\");return}}h=a.parse(h,"
                << std::endl ;
            out
                << "d);c(h.geometry,h.materials)}else THREE.error(\"THREE.JSONLoader: \"+b+\" seems to be unreachable or the file is empty.\");a.onLoadComplete()}else THREE.error(\"THREE.JSONLoader: Couldn't load \"+b+\" (\"+f.status+\")\");else f.readyState===f.LOADING?e&&(0===g&&(g=f.getResponseHeader(\"Content-Length\")),e({total:g,loaded:f.responseText.length})):f.readyState===f.HEADERS_RECEIVED&&void 0!==e&&(g=f.getResponseHeader(\"Content-Length\"))};f.open(\"GET\",b,!0);f.withCredentials=this.withCredentials;f.send(null)};"
                << std::endl ;
            out
                << "THREE.JSONLoader.prototype.parse=function(a,b){var c=new THREE.Geometry,d=void 0!==a.scale?1/a.scale:1;(function(b){var d,g,h,k,l,p,q,n,t,r,s,u,v,x=a.faces;p=a.vertices;var D=a.normals,w=a.colors,y=0;if(void 0!==a.uvs){for(d=0;d<a.uvs.length;d++)a.uvs[d].length&&y++;for(d=0;d<y;d++)c.faceVertexUvs[d]=[]}k=0;for(l=p.length;k<l;)d=new THREE.Vector3,d.x=p[k++]*b,d.y=p[k++]*b,d.z=p[k++]*b,c.vertices.push(d);k=0;for(l=x.length;k<l;)if(b=x[k++],t=b&1,h=b&2,d=b&8,q=b&16,r=b&32,p=b&64,b&=128,t){t=new THREE.Face3;"
                << std::endl ;
            out
                << "t.a=x[k];t.b=x[k+1];t.c=x[k+3];s=new THREE.Face3;s.a=x[k+1];s.b=x[k+2];s.c=x[k+3];k+=4;h&&(h=x[k++],t.materialIndex=h,s.materialIndex=h);h=c.faces.length;if(d)for(d=0;d<y;d++)for(u=a.uvs[d],c.faceVertexUvs[d][h]=[],c.faceVertexUvs[d][h+1]=[],g=0;4>g;g++)n=x[k++],v=u[2*n],n=u[2*n+1],v=new THREE.Vector2(v,n),2!==g&&c.faceVertexUvs[d][h].push(v),0!==g&&c.faceVertexUvs[d][h+1].push(v);q&&(q=3*x[k++],t.normal.set(D[q++],D[q++],D[q]),s.normal.copy(t.normal));if(r)for(d=0;4>d;d++)q=3*x[k++],r=new THREE.Vector3(D[q++],"
                << std::endl ;
            out
                << "D[q++],D[q]),2!==d&&t.vertexNormals.push(r),0!==d&&s.vertexNormals.push(r);p&&(p=x[k++],p=w[p],t.color.setHex(p),s.color.setHex(p));if(b)for(d=0;4>d;d++)p=x[k++],p=w[p],2!==d&&t.vertexColors.push(new THREE.Color(p)),0!==d&&s.vertexColors.push(new THREE.Color(p));c.faces.push(t);c.faces.push(s)}else{t=new THREE.Face3;t.a=x[k++];t.b=x[k++];t.c=x[k++];h&&(h=x[k++],t.materialIndex=h);h=c.faces.length;if(d)for(d=0;d<y;d++)for(u=a.uvs[d],c.faceVertexUvs[d][h]=[],g=0;3>g;g++)n=x[k++],v=u[2*n],n=u[2*n+1],"
                << std::endl ;
            out
                << "v=new THREE.Vector2(v,n),c.faceVertexUvs[d][h].push(v);q&&(q=3*x[k++],t.normal.set(D[q++],D[q++],D[q]));if(r)for(d=0;3>d;d++)q=3*x[k++],r=new THREE.Vector3(D[q++],D[q++],D[q]),t.vertexNormals.push(r);p&&(p=x[k++],t.color.setHex(w[p]));if(b)for(d=0;3>d;d++)p=x[k++],t.vertexColors.push(new THREE.Color(w[p]));c.faces.push(t)}})(d);(function(){var b=void 0!==a.influencesPerVertex?a.influencesPerVertex:2;if(a.skinWeights)for(var d=0,g=a.skinWeights.length;d<g;d+=b)c.skinWeights.push(new THREE.Vector4(a.skinWeights[d],"
                << std::endl ;
            out
                << "1<b?a.skinWeights[d+1]:0,2<b?a.skinWeights[d+2]:0,3<b?a.skinWeights[d+3]:0));if(a.skinIndices)for(d=0,g=a.skinIndices.length;d<g;d+=b)c.skinIndices.push(new THREE.Vector4(a.skinIndices[d],1<b?a.skinIndices[d+1]:0,2<b?a.skinIndices[d+2]:0,3<b?a.skinIndices[d+3]:0));c.bones=a.bones;c.bones&&0<c.bones.length&&(c.skinWeights.length!==c.skinIndices.length||c.skinIndices.length!==c.vertices.length)&&THREE.warn(\"THREE.JSONLoader: When skinning, number of vertices (\"+c.vertices.length+\"), skinIndices (\"+"
                << std::endl ;
            out
                << "c.skinIndices.length+\"), and skinWeights (\"+c.skinWeights.length+\") should match.\");c.animation=a.animation;c.animations=a.animations})();(function(b){if(void 0!==a.morphTargets){var d,g,h,k,l,p;d=0;for(g=a.morphTargets.length;d<g;d++)for(c.morphTargets[d]={},c.morphTargets[d].name=a.morphTargets[d].name,c.morphTargets[d].vertices=[],l=c.morphTargets[d].vertices,p=a.morphTargets[d].vertices,h=0,k=p.length;h<k;h+=3){var q=new THREE.Vector3;q.x=p[h]*b;q.y=p[h+1]*b;q.z=p[h+2]*b;l.push(q)}}if(void 0!=="
                << std::endl ;
            out
                << "a.morphColors)for(d=0,g=a.morphColors.length;d<g;d++)for(c.morphColors[d]={},c.morphColors[d].name=a.morphColors[d].name,c.morphColors[d].colors=[],k=c.morphColors[d].colors,l=a.morphColors[d].colors,b=0,h=l.length;b<h;b+=3)p=new THREE.Color(16755200),p.setRGB(l[b],l[b+1],l[b+2]),k.push(p)})(d);c.computeFaceNormals();c.computeBoundingSphere();if(void 0===a.materials||0===a.materials.length)return{geometry:c};d=this.initMaterials(a.materials,b);this.needsTangents(d)&&c.computeTangents();return{geometry:c,"
                << std::endl ;
            out
                << "materials:d}};THREE.LoadingManager=function(a,b,c){var d=this,e=0,f=0;this.onLoad=a;this.onProgress=b;this.onError=c;this.itemStart=function(a){f++};this.itemEnd=function(a){e++;if(void 0!==d.onProgress)d.onProgress(a,e,f);if(e===f&&void 0!==d.onLoad)d.onLoad()}};THREE.DefaultLoadingManager=new THREE.LoadingManager;THREE.BufferGeometryLoader=function(a){this.manager=void 0!==a?a:THREE.DefaultLoadingManager};"
                << std::endl ;
            out
                << "THREE.BufferGeometryLoader.prototype={constructor:THREE.BufferGeometryLoader,load:function(a,b,c,d){var e=this,f=new THREE.XHRLoader(e.manager);f.setCrossOrigin(this.crossOrigin);f.load(a,function(a){b(e.parse(JSON.parse(a)))},c,d)},setCrossOrigin:function(a){this.crossOrigin=a},parse:function(a){var b=new THREE.BufferGeometry,c=a.data.attributes,d;for(d in c){var e=c[d],f=new self[e.type](e.array);b.addAttribute(d,new THREE.BufferAttribute(f,e.itemSize))}c=a.data.offsets;void 0!==c&&(b.offsets=JSON.parse(JSON.stringify(c)));"
                << std::endl ;
            out
                << "a=a.data.boundingSphere;void 0!==a&&(c=new THREE.Vector3,void 0!==a.center&&c.fromArray(a.center),b.boundingSphere=new THREE.Sphere(c,a.radius));return b}};THREE.MaterialLoader=function(a){this.manager=void 0!==a?a:THREE.DefaultLoadingManager};"
                << std::endl ;
            out
                << "THREE.MaterialLoader.prototype={constructor:THREE.MaterialLoader,load:function(a,b,c,d){var e=this,f=new THREE.XHRLoader(e.manager);f.setCrossOrigin(this.crossOrigin);f.load(a,function(a){b(e.parse(JSON.parse(a)))},c,d)},setCrossOrigin:function(a){this.crossOrigin=a},parse:function(a){var b=new THREE[a.type];void 0!==a.color&&b.color.setHex(a.color);void 0!==a.emissive&&b.emissive.setHex(a.emissive);void 0!==a.specular&&b.specular.setHex(a.specular);void 0!==a.shininess&&(b.shininess=a.shininess);"
                << std::endl ;
            out
                << "void 0!==a.uniforms&&(b.uniforms=a.uniforms);void 0!==a.vertexShader&&(b.vertexShader=a.vertexShader);void 0!==a.fragmentShader&&(b.fragmentShader=a.fragmentShader);void 0!==a.vertexColors&&(b.vertexColors=a.vertexColors);void 0!==a.shading&&(b.shading=a.shading);void 0!==a.blending&&(b.blending=a.blending);void 0!==a.side&&(b.side=a.side);void 0!==a.opacity&&(b.opacity=a.opacity);void 0!==a.transparent&&(b.transparent=a.transparent);void 0!==a.wireframe&&(b.wireframe=a.wireframe);void 0!==a.size&&"
                << std::endl ;
            out
                << "(b.size=a.size);void 0!==a.sizeAttenuation&&(b.sizeAttenuation=a.sizeAttenuation);if(void 0!==a.materials)for(var c=0,d=a.materials.length;c<d;c++)b.materials.push(this.parse(a.materials[c]));return b}};THREE.ObjectLoader=function(a){this.manager=void 0!==a?a:THREE.DefaultLoadingManager;this.texturePath=\"\"};"
                << std::endl ;
            out
                << "THREE.ObjectLoader.prototype={constructor:THREE.ObjectLoader,load:function(a,b,c,d){\"\"===this.texturePath&&(this.texturePath=a.substring(0,a.lastIndexOf(\"/\")+1));var e=this,f=new THREE.XHRLoader(e.manager);f.setCrossOrigin(this.crossOrigin);f.load(a,function(a){e.parse(JSON.parse(a),b)},c,d)},setTexturePath:function(a){this.texturePath=a},setCrossOrigin:function(a){this.crossOrigin=a},parse:function(a,b){var c=this.parseGeometries(a.geometries),d=this.parseImages(a.images,function(){void 0!==b&&b(e)}),"
                << std::endl ;
            out
                << "d=this.parseTextures(a.textures,d),d=this.parseMaterials(a.materials,d),e=this.parseObject(a.object,c,d);void 0!==a.images&&0!==a.images.length||void 0===b||b(e);return e},parseGeometries:function(a){var b={};if(void 0!==a)for(var c=new THREE.JSONLoader,d=new THREE.BufferGeometryLoader,e=0,f=a.length;e<f;e++){var g,h=a[e];switch(h.type){case \"PlaneGeometry\":case \"PlaneBufferGeometry\":g=new THREE[h.type](h.width,h.height,h.widthSegments,h.heightSegments);break;case \"BoxGeometry\":case \"CubeGeometry\":g="
                << std::endl ;
            out
                << "new THREE.BoxGeometry(h.width,h.height,h.depth,h.widthSegments,h.heightSegments,h.depthSegments);break;case \"CircleGeometry\":g=new THREE.CircleGeometry(h.radius,h.segments);break;case \"CylinderGeometry\":g=new THREE.CylinderGeometry(h.radiusTop,h.radiusBottom,h.height,h.radialSegments,h.heightSegments,h.openEnded);break;case \"SphereGeometry\":g=new THREE.SphereGeometry(h.radius,h.widthSegments,h.heightSegments,h.phiStart,h.phiLength,h.thetaStart,h.thetaLength);break;case \"IcosahedronGeometry\":g=new THREE.IcosahedronGeometry(h.radius,"
                << std::endl ;
            out
                << "h.detail);break;case \"TorusGeometry\":g=new THREE.TorusGeometry(h.radius,h.tube,h.radialSegments,h.tubularSegments,h.arc);break;case \"TorusKnotGeometry\":g=new THREE.TorusKnotGeometry(h.radius,h.tube,h.radialSegments,h.tubularSegments,h.p,h.q,h.heightScale);break;case \"BufferGeometry\":g=d.parse(h);break;case \"Geometry\":g=c.parse(h.data).geometry}g.uuid=h.uuid;void 0!==h.name&&(g.name=h.name);b[h.uuid]=g}return b},parseMaterials:function(a,b){var c={};if(void 0!==a)for(var d=function(a){void 0===b[a]&&"
                << std::endl ;
            out
                << "THREE.warn(\"THREE.ObjectLoader: Undefined texture\",a);return b[a]},e=new THREE.MaterialLoader,f=0,g=a.length;f<g;f++){var h=a[f],k=e.parse(h);k.uuid=h.uuid;void 0!==h.name&&(k.name=h.name);void 0!==h.map&&(k.map=d(h.map));void 0!==h.bumpMap&&(k.bumpMap=d(h.bumpMap),h.bumpScale&&(k.bumpScale=new THREE.Vector2(h.bumpScale,h.bumpScale)));void 0!==h.alphaMap&&(k.alphaMap=d(h.alphaMap));void 0!==h.envMap&&(k.envMap=d(h.envMap));void 0!==h.normalMap&&(k.normalMap=d(h.normalMap),h.normalScale&&(k.normalScale="
                << std::endl ;
            out
                << "new THREE.Vector2(h.normalScale,h.normalScale)));void 0!==h.lightMap&&(k.lightMap=d(h.lightMap));void 0!==h.specularMap&&(k.specularMap=d(h.specularMap));c[h.uuid]=k}return c},parseImages:function(a,b){var c=this,d={};if(void 0!==a&&0<a.length){var e=new THREE.LoadingManager(b),f=new THREE.ImageLoader(e);f.setCrossOrigin(this.crossOrigin);for(var e=function(a){c.manager.itemStart(a);return f.load(a,function(){c.manager.itemEnd(a)})},g=0,h=a.length;g<h;g++){var k=a[g],l=/^(\\/\\/)|([a-z]+:(\\/\\/)?)/i.test(k.url)?"
                << std::endl ;
            out
                << "k.url:c.texturePath+k.url;d[k.uuid]=e(l)}}return d},parseTextures:function(a,b){var c={};if(void 0!==a)for(var d=0,e=a.length;d<e;d++){var f=a[d];void 0===f.image&&THREE.warn('THREE.ObjectLoader: No \"image\" speficied for',f.uuid);void 0===b[f.image]&&THREE.warn(\"THREE.ObjectLoader: Undefined image\",f.image);var g=new THREE.Texture(b[f.image]);g.needsUpdate=!0;g.uuid=f.uuid;void 0!==f.name&&(g.name=f.name);void 0!==f.repeat&&(g.repeat=new THREE.Vector2(f.repeat[0],f.repeat[1]));void 0!==f.minFilter&&"
                << std::endl ;
            out
                << "(g.minFilter=THREE[f.minFilter]);void 0!==f.magFilter&&(g.magFilter=THREE[f.magFilter]);void 0!==f.anisotropy&&(g.anisotropy=f.anisotropy);f.wrap instanceof Array&&(g.wrapS=THREE[f.wrap[0]],g.wrapT=THREE[f.wrap[1]]);c[f.uuid]=g}return c},parseObject:function(){var a=new THREE.Matrix4;return function(b,c,d){var e;e=function(a){void 0===c[a]&&THREE.warn(\"THREE.ObjectLoader: Undefined geometry\",a);return c[a]};var f=function(a){void 0===d[a]&&THREE.warn(\"THREE.ObjectLoader: Undefined material\",a);return d[a]};"
                << std::endl ;
            out
                << "switch(b.type){case \"Scene\":e=new THREE.Scene;break;case \"PerspectiveCamera\":e=new THREE.PerspectiveCamera(b.fov,b.aspect,b.near,b.far);break;case \"OrthographicCamera\":e=new THREE.OrthographicCamera(b.left,b.right,b.top,b.bottom,b.near,b.far);break;case \"AmbientLight\":e=new THREE.AmbientLight(b.color);break;case \"DirectionalLight\":e=new THREE.DirectionalLight(b.color,b.intensity);break;case \"PointLight\":e=new THREE.PointLight(b.color,b.intensity,b.distance,b.decay);break;case \"SpotLight\":e=new THREE.SpotLight(b.color,"
                << std::endl ;
            out
                << "b.intensity,b.distance,b.angle,b.exponent,b.decay);break;case \"HemisphereLight\":e=new THREE.HemisphereLight(b.color,b.groundColor,b.intensity);break;case \"Mesh\":e=new THREE.Mesh(e(b.geometry),f(b.material));break;case \"Line\":e=new THREE.Line(e(b.geometry),f(b.material),b.mode);break;case \"PointCloud\":e=new THREE.PointCloud(e(b.geometry),f(b.material));break;case \"Sprite\":e=new THREE.Sprite(f(b.material));break;case \"Group\":e=new THREE.Group;break;default:e=new THREE.Object3D}e.uuid=b.uuid;void 0!=="
                << std::endl ;
            out
                << "b.name&&(e.name=b.name);void 0!==b.matrix?(a.fromArray(b.matrix),a.decompose(e.position,e.quaternion,e.scale)):(void 0!==b.position&&e.position.fromArray(b.position),void 0!==b.rotation&&e.rotation.fromArray(b.rotation),void 0!==b.scale&&e.scale.fromArray(b.scale));void 0!==b.visible&&(e.visible=b.visible);void 0!==b.userData&&(e.userData=b.userData);if(void 0!==b.children)for(var g in b.children)e.add(this.parseObject(b.children[g],c,d));return e}}()};"
                << std::endl ;
            out
                << "THREE.TextureLoader=function(a){this.manager=void 0!==a?a:THREE.DefaultLoadingManager};THREE.TextureLoader.prototype={constructor:THREE.TextureLoader,load:function(a,b,c,d){var e=new THREE.ImageLoader(this.manager);e.setCrossOrigin(this.crossOrigin);e.load(a,function(a){a=new THREE.Texture(a);a.needsUpdate=!0;void 0!==b&&b(a)},c,d)},setCrossOrigin:function(a){this.crossOrigin=a}};THREE.DataTextureLoader=THREE.BinaryTextureLoader=function(){this._parser=null};"
                << std::endl ;
            out
                << "THREE.BinaryTextureLoader.prototype={constructor:THREE.BinaryTextureLoader,load:function(a,b,c,d){var e=this,f=new THREE.DataTexture,g=new THREE.XHRLoader;g.setResponseType(\"arraybuffer\");g.load(a,function(a){if(a=e._parser(a))void 0!==a.image?f.image=a.image:void 0!==a.data&&(f.image.width=a.width,f.image.height=a.height,f.image.data=a.data),f.wrapS=void 0!==a.wrapS?a.wrapS:THREE.ClampToEdgeWrapping,f.wrapT=void 0!==a.wrapT?a.wrapT:THREE.ClampToEdgeWrapping,f.magFilter=void 0!==a.magFilter?a.magFilter:"
                << std::endl ;
            out
                << "THREE.LinearFilter,f.minFilter=void 0!==a.minFilter?a.minFilter:THREE.LinearMipMapLinearFilter,f.anisotropy=void 0!==a.anisotropy?a.anisotropy:1,void 0!==a.format&&(f.format=a.format),void 0!==a.type&&(f.type=a.type),void 0!==a.mipmaps&&(f.mipmaps=a.mipmaps),1===a.mipmapCount&&(f.minFilter=THREE.LinearFilter),f.needsUpdate=!0,b&&b(f,a)},c,d);return f}};THREE.CompressedTextureLoader=function(){this._parser=null};"
                << std::endl ;
            out
                << "THREE.CompressedTextureLoader.prototype={constructor:THREE.CompressedTextureLoader,load:function(a,b,c){var d=this,e=[],f=new THREE.CompressedTexture;f.image=e;var g=new THREE.XHRLoader;g.setResponseType(\"arraybuffer\");if(a instanceof Array){var h=0;c=function(c){g.load(a[c],function(a){a=d._parser(a,!0);e[c]={width:a.width,height:a.height,format:a.format,mipmaps:a.mipmaps};h+=1;6===h&&(1==a.mipmapCount&&(f.minFilter=THREE.LinearFilter),f.format=a.format,f.needsUpdate=!0,b&&b(f))})};for(var k=0,l="
                << std::endl ;
            out
                << "a.length;k<l;++k)c(k)}else g.load(a,function(a){a=d._parser(a,!0);if(a.isCubemap)for(var c=a.mipmaps.length/a.mipmapCount,g=0;g<c;g++){e[g]={mipmaps:[]};for(var h=0;h<a.mipmapCount;h++)e[g].mipmaps.push(a.mipmaps[g*a.mipmapCount+h]),e[g].format=a.format,e[g].width=a.width,e[g].height=a.height}else f.image.width=a.width,f.image.height=a.height,f.mipmaps=a.mipmaps;1===a.mipmapCount&&(f.minFilter=THREE.LinearFilter);f.format=a.format;f.needsUpdate=!0;b&&b(f)});return f}};"
                << std::endl ;
            out
                << "THREE.Material=function(){Object.defineProperty(this,\"id\",{value:THREE.MaterialIdCount++});this.uuid=THREE.Math.generateUUID();this.name=\"\";this.type=\"Material\";this.side=THREE.FrontSide;this.opacity=1;this.transparent=!1;this.blending=THREE.NormalBlending;this.blendSrc=THREE.SrcAlphaFactor;this.blendDst=THREE.OneMinusSrcAlphaFactor;this.blendEquation=THREE.AddEquation;this.blendEquationAlpha=this.blendDstAlpha=this.blendSrcAlpha=null;this.colorWrite=this.depthWrite=this.depthTest=!0;this.polygonOffset="
                << std::endl ;
            out
                << "!1;this.overdraw=this.alphaTest=this.polygonOffsetUnits=this.polygonOffsetFactor=0;this._needsUpdate=this.visible=!0};"
                << std::endl ;
            out
                << "THREE.Material.prototype={constructor:THREE.Material,get needsUpdate(){return this._needsUpdate},set needsUpdate(a){!0===a&&this.update();this._needsUpdate=a},setValues:function(a){if(void 0!==a)for(var b in a){var c=a[b];if(void 0===c)THREE.warn(\"THREE.Material: '\"+b+\"' parameter is undefined.\");else if(b in this){var d=this[b];d instanceof THREE.Color?d.set(c):d instanceof THREE.Vector3&&c instanceof THREE.Vector3?d.copy(c):this[b]=\"overdraw\"==b?Number(c):c}}},toJSON:function(){var a={metadata:{version:4.2,"
                << std::endl ;
            out
                << "type:\"material\",generator:\"MaterialExporter\"},uuid:this.uuid,type:this.type};\"\"!==this.name&&(a.name=this.name);this instanceof THREE.MeshBasicMaterial?(a.color=this.color.getHex(),this.vertexColors!==THREE.NoColors&&(a.vertexColors=this.vertexColors),this.blending!==THREE.NormalBlending&&(a.blending=this.blending),this.side!==THREE.FrontSide&&(a.side=this.side)):this instanceof THREE.MeshLambertMaterial?(a.color=this.color.getHex(),a.emissive=this.emissive.getHex(),this.vertexColors!==THREE.NoColors&&"
                << std::endl ;
            out
                << "(a.vertexColors=this.vertexColors),this.shading!==THREE.SmoothShading&&(a.shading=this.shading),this.blending!==THREE.NormalBlending&&(a.blending=this.blending),this.side!==THREE.FrontSide&&(a.side=this.side)):this instanceof THREE.MeshPhongMaterial?(a.color=this.color.getHex(),a.emissive=this.emissive.getHex(),a.specular=this.specular.getHex(),a.shininess=this.shininess,this.vertexColors!==THREE.NoColors&&(a.vertexColors=this.vertexColors),this.shading!==THREE.SmoothShading&&(a.shading=this.shading),"
                << std::endl ;
            out
                << "this.blending!==THREE.NormalBlending&&(a.blending=this.blending),this.side!==THREE.FrontSide&&(a.side=this.side)):this instanceof THREE.MeshNormalMaterial?(this.blending!==THREE.NormalBlending&&(a.blending=this.blending),this.side!==THREE.FrontSide&&(a.side=this.side)):this instanceof THREE.MeshDepthMaterial?(this.blending!==THREE.NormalBlending&&(a.blending=this.blending),this.side!==THREE.FrontSide&&(a.side=this.side)):this instanceof THREE.PointCloudMaterial?(a.size=this.size,a.sizeAttenuation="
                << std::endl ;
            out
                << "this.sizeAttenuation,a.color=this.color.getHex(),this.vertexColors!==THREE.NoColors&&(a.vertexColors=this.vertexColors),this.blending!==THREE.NormalBlending&&(a.blending=this.blending)):this instanceof THREE.ShaderMaterial?(a.uniforms=this.uniforms,a.vertexShader=this.vertexShader,a.fragmentShader=this.fragmentShader):this instanceof THREE.SpriteMaterial&&(a.color=this.color.getHex());1>this.opacity&&(a.opacity=this.opacity);!1!==this.transparent&&(a.transparent=this.transparent);!1!==this.wireframe&&"
                << std::endl ;
            out
                << "(a.wireframe=this.wireframe);return a},clone:function(a){void 0===a&&(a=new THREE.Material);a.name=this.name;a.side=this.side;a.opacity=this.opacity;a.transparent=this.transparent;a.blending=this.blending;a.blendSrc=this.blendSrc;a.blendDst=this.blendDst;a.blendEquation=this.blendEquation;a.blendSrcAlpha=this.blendSrcAlpha;a.blendDstAlpha=this.blendDstAlpha;a.blendEquationAlpha=this.blendEquationAlpha;a.depthTest=this.depthTest;a.depthWrite=this.depthWrite;a.polygonOffset=this.polygonOffset;a.polygonOffsetFactor="
                << std::endl ;
            out
                << "this.polygonOffsetFactor;a.polygonOffsetUnits=this.polygonOffsetUnits;a.alphaTest=this.alphaTest;a.overdraw=this.overdraw;a.visible=this.visible;return a},update:function(){this.dispatchEvent({type:\"update\"})},dispose:function(){this.dispatchEvent({type:\"dispose\"})}};THREE.EventDispatcher.prototype.apply(THREE.Material.prototype);THREE.MaterialIdCount=0;"
                << std::endl ;
            out
                << "THREE.LineBasicMaterial=function(a){THREE.Material.call(this);this.type=\"LineBasicMaterial\";this.color=new THREE.Color(16777215);this.linewidth=1;this.linejoin=this.linecap=\"round\";this.vertexColors=THREE.NoColors;this.fog=!0;this.setValues(a)};THREE.LineBasicMaterial.prototype=Object.create(THREE.Material.prototype);THREE.LineBasicMaterial.prototype.constructor=THREE.LineBasicMaterial;"
                << std::endl ;
            out
                << "THREE.LineBasicMaterial.prototype.clone=function(){var a=new THREE.LineBasicMaterial;THREE.Material.prototype.clone.call(this,a);a.color.copy(this.color);a.linewidth=this.linewidth;a.linecap=this.linecap;a.linejoin=this.linejoin;a.vertexColors=this.vertexColors;a.fog=this.fog;return a};"
                << std::endl ;
            out
                << "THREE.LineDashedMaterial=function(a){THREE.Material.call(this);this.type=\"LineDashedMaterial\";this.color=new THREE.Color(16777215);this.scale=this.linewidth=1;this.dashSize=3;this.gapSize=1;this.vertexColors=!1;this.fog=!0;this.setValues(a)};THREE.LineDashedMaterial.prototype=Object.create(THREE.Material.prototype);THREE.LineDashedMaterial.prototype.constructor=THREE.LineDashedMaterial;"
                << std::endl ;
            out
                << "THREE.LineDashedMaterial.prototype.clone=function(){var a=new THREE.LineDashedMaterial;THREE.Material.prototype.clone.call(this,a);a.color.copy(this.color);a.linewidth=this.linewidth;a.scale=this.scale;a.dashSize=this.dashSize;a.gapSize=this.gapSize;a.vertexColors=this.vertexColors;a.fog=this.fog;return a};"
                << std::endl ;
            out
                << "THREE.MeshBasicMaterial=function(a){THREE.Material.call(this);this.type=\"MeshBasicMaterial\";this.color=new THREE.Color(16777215);this.envMap=this.alphaMap=this.specularMap=this.lightMap=this.map=null;this.combine=THREE.MultiplyOperation;this.reflectivity=1;this.refractionRatio=.98;this.fog=!0;this.shading=THREE.SmoothShading;this.wireframe=!1;this.wireframeLinewidth=1;this.wireframeLinejoin=this.wireframeLinecap=\"round\";this.vertexColors=THREE.NoColors;this.morphTargets=this.skinning=!1;this.setValues(a)};"
                << std::endl ;
            out
                << "THREE.MeshBasicMaterial.prototype=Object.create(THREE.Material.prototype);THREE.MeshBasicMaterial.prototype.constructor=THREE.MeshBasicMaterial;"
                << std::endl ;
            out
                << "THREE.MeshBasicMaterial.prototype.clone=function(){var a=new THREE.MeshBasicMaterial;THREE.Material.prototype.clone.call(this,a);a.color.copy(this.color);a.map=this.map;a.lightMap=this.lightMap;a.specularMap=this.specularMap;a.alphaMap=this.alphaMap;a.envMap=this.envMap;a.combine=this.combine;a.reflectivity=this.reflectivity;a.refractionRatio=this.refractionRatio;a.fog=this.fog;a.shading=this.shading;a.wireframe=this.wireframe;a.wireframeLinewidth=this.wireframeLinewidth;a.wireframeLinecap=this.wireframeLinecap;"
                << std::endl ;
            out
                << "a.wireframeLinejoin=this.wireframeLinejoin;a.vertexColors=this.vertexColors;a.skinning=this.skinning;a.morphTargets=this.morphTargets;return a};"
                << std::endl ;
            out
                << "THREE.MeshLambertMaterial=function(a){THREE.Material.call(this);this.type=\"MeshLambertMaterial\";this.color=new THREE.Color(16777215);this.emissive=new THREE.Color(0);this.wrapAround=!1;this.wrapRGB=new THREE.Vector3(1,1,1);this.envMap=this.alphaMap=this.specularMap=this.lightMap=this.map=null;this.combine=THREE.MultiplyOperation;this.reflectivity=1;this.refractionRatio=.98;this.fog=!0;this.shading=THREE.SmoothShading;this.wireframe=!1;this.wireframeLinewidth=1;this.wireframeLinejoin=this.wireframeLinecap="
                << std::endl ;
            out
                << "\"round\";this.vertexColors=THREE.NoColors;this.morphNormals=this.morphTargets=this.skinning=!1;this.setValues(a)};THREE.MeshLambertMaterial.prototype=Object.create(THREE.Material.prototype);THREE.MeshLambertMaterial.prototype.constructor=THREE.MeshLambertMaterial;"
                << std::endl ;
            out
                << "THREE.MeshLambertMaterial.prototype.clone=function(){var a=new THREE.MeshLambertMaterial;THREE.Material.prototype.clone.call(this,a);a.color.copy(this.color);a.emissive.copy(this.emissive);a.wrapAround=this.wrapAround;a.wrapRGB.copy(this.wrapRGB);a.map=this.map;a.lightMap=this.lightMap;a.specularMap=this.specularMap;a.alphaMap=this.alphaMap;a.envMap=this.envMap;a.combine=this.combine;a.reflectivity=this.reflectivity;a.refractionRatio=this.refractionRatio;a.fog=this.fog;a.shading=this.shading;a.wireframe="
                << std::endl ;
            out
                << "this.wireframe;a.wireframeLinewidth=this.wireframeLinewidth;a.wireframeLinecap=this.wireframeLinecap;a.wireframeLinejoin=this.wireframeLinejoin;a.vertexColors=this.vertexColors;a.skinning=this.skinning;a.morphTargets=this.morphTargets;a.morphNormals=this.morphNormals;return a};"
                << std::endl ;
            out
                << "THREE.MeshPhongMaterial=function(a){THREE.Material.call(this);this.type=\"MeshPhongMaterial\";this.color=new THREE.Color(16777215);this.emissive=new THREE.Color(0);this.specular=new THREE.Color(1118481);this.shininess=30;this.wrapAround=this.metal=!1;this.wrapRGB=new THREE.Vector3(1,1,1);this.bumpMap=this.lightMap=this.map=null;this.bumpScale=1;this.normalMap=null;this.normalScale=new THREE.Vector2(1,1);this.envMap=this.alphaMap=this.specularMap=null;this.combine=THREE.MultiplyOperation;this.reflectivity="
                << std::endl ;
            out
                << "1;this.refractionRatio=.98;this.fog=!0;this.shading=THREE.SmoothShading;this.wireframe=!1;this.wireframeLinewidth=1;this.wireframeLinejoin=this.wireframeLinecap=\"round\";this.vertexColors=THREE.NoColors;this.morphNormals=this.morphTargets=this.skinning=!1;this.setValues(a)};THREE.MeshPhongMaterial.prototype=Object.create(THREE.Material.prototype);THREE.MeshPhongMaterial.prototype.constructor=THREE.MeshPhongMaterial;"
                << std::endl ;
            out
                << "THREE.MeshPhongMaterial.prototype.clone=function(){var a=new THREE.MeshPhongMaterial;THREE.Material.prototype.clone.call(this,a);a.color.copy(this.color);a.emissive.copy(this.emissive);a.specular.copy(this.specular);a.shininess=this.shininess;a.metal=this.metal;a.wrapAround=this.wrapAround;a.wrapRGB.copy(this.wrapRGB);a.map=this.map;a.lightMap=this.lightMap;a.bumpMap=this.bumpMap;a.bumpScale=this.bumpScale;a.normalMap=this.normalMap;a.normalScale.copy(this.normalScale);a.specularMap=this.specularMap;"
                << std::endl ;
            out
                << "a.alphaMap=this.alphaMap;a.envMap=this.envMap;a.combine=this.combine;a.reflectivity=this.reflectivity;a.refractionRatio=this.refractionRatio;a.fog=this.fog;a.shading=this.shading;a.wireframe=this.wireframe;a.wireframeLinewidth=this.wireframeLinewidth;a.wireframeLinecap=this.wireframeLinecap;a.wireframeLinejoin=this.wireframeLinejoin;a.vertexColors=this.vertexColors;a.skinning=this.skinning;a.morphTargets=this.morphTargets;a.morphNormals=this.morphNormals;return a};"
                << std::endl ;
            out
                << "THREE.MeshDepthMaterial=function(a){THREE.Material.call(this);this.type=\"MeshDepthMaterial\";this.wireframe=this.morphTargets=!1;this.wireframeLinewidth=1;this.setValues(a)};THREE.MeshDepthMaterial.prototype=Object.create(THREE.Material.prototype);THREE.MeshDepthMaterial.prototype.constructor=THREE.MeshDepthMaterial;"
                << std::endl ;
            out
                << "THREE.MeshDepthMaterial.prototype.clone=function(){var a=new THREE.MeshDepthMaterial;THREE.Material.prototype.clone.call(this,a);a.wireframe=this.wireframe;a.wireframeLinewidth=this.wireframeLinewidth;return a};THREE.MeshNormalMaterial=function(a){THREE.Material.call(this,a);this.type=\"MeshNormalMaterial\";this.wireframe=!1;this.wireframeLinewidth=1;this.morphTargets=!1;this.setValues(a)};THREE.MeshNormalMaterial.prototype=Object.create(THREE.Material.prototype);"
                << std::endl ;
            out
                << "THREE.MeshNormalMaterial.prototype.constructor=THREE.MeshNormalMaterial;THREE.MeshNormalMaterial.prototype.clone=function(){var a=new THREE.MeshNormalMaterial;THREE.Material.prototype.clone.call(this,a);a.wireframe=this.wireframe;a.wireframeLinewidth=this.wireframeLinewidth;return a};THREE.MeshFaceMaterial=function(a){this.uuid=THREE.Math.generateUUID();this.type=\"MeshFaceMaterial\";this.materials=a instanceof Array?a:[]};"
                << std::endl ;
            out
                << "THREE.MeshFaceMaterial.prototype={constructor:THREE.MeshFaceMaterial,toJSON:function(){for(var a={metadata:{version:4.2,type:\"material\",generator:\"MaterialExporter\"},uuid:this.uuid,type:this.type,materials:[]},b=0,c=this.materials.length;b<c;b++)a.materials.push(this.materials[b].toJSON());return a},clone:function(){for(var a=new THREE.MeshFaceMaterial,b=0;b<this.materials.length;b++)a.materials.push(this.materials[b].clone());return a}};"
                << std::endl ;
            out
                << "THREE.PointCloudMaterial=function(a){THREE.Material.call(this);this.type=\"PointCloudMaterial\";this.color=new THREE.Color(16777215);this.map=null;this.size=1;this.sizeAttenuation=!0;this.vertexColors=THREE.NoColors;this.fog=!0;this.setValues(a)};THREE.PointCloudMaterial.prototype=Object.create(THREE.Material.prototype);THREE.PointCloudMaterial.prototype.constructor=THREE.PointCloudMaterial;"
                << std::endl ;
            out
                << "THREE.PointCloudMaterial.prototype.clone=function(){var a=new THREE.PointCloudMaterial;THREE.Material.prototype.clone.call(this,a);a.color.copy(this.color);a.map=this.map;a.size=this.size;a.sizeAttenuation=this.sizeAttenuation;a.vertexColors=this.vertexColors;a.fog=this.fog;return a};THREE.ParticleBasicMaterial=function(a){THREE.warn(\"THREE.ParticleBasicMaterial has been renamed to THREE.PointCloudMaterial.\");return new THREE.PointCloudMaterial(a)};"
                << std::endl ;
            out
                << "THREE.ParticleSystemMaterial=function(a){THREE.warn(\"THREE.ParticleSystemMaterial has been renamed to THREE.PointCloudMaterial.\");return new THREE.PointCloudMaterial(a)};"
                << std::endl ;
            out
                << "THREE.ShaderMaterial=function(a){THREE.Material.call(this);this.type=\"ShaderMaterial\";this.defines={};this.uniforms={};this.attributes=null;this.vertexShader=\"void main() {\\n\\tgl_Position = projectionMatrix * modelViewMatrix * vec4( position, 1.0 );\\n}\";this.fragmentShader=\"void main() {\\n\\tgl_FragColor = vec4( 1.0, 0.0, 0.0, 1.0 );\\n}\";this.shading=THREE.SmoothShading;this.linewidth=1;this.wireframe=!1;this.wireframeLinewidth=1;this.lights=this.fog=!1;this.vertexColors=THREE.NoColors;this.morphNormals="
                << std::endl ;
            out
                << "this.morphTargets=this.skinning=!1;this.defaultAttributeValues={color:[1,1,1],uv:[0,0],uv2:[0,0]};this.index0AttributeName=void 0;this.setValues(a)};THREE.ShaderMaterial.prototype=Object.create(THREE.Material.prototype);THREE.ShaderMaterial.prototype.constructor=THREE.ShaderMaterial;"
                << std::endl ;
            out
                << "THREE.ShaderMaterial.prototype.clone=function(){var a=new THREE.ShaderMaterial;THREE.Material.prototype.clone.call(this,a);a.fragmentShader=this.fragmentShader;a.vertexShader=this.vertexShader;a.uniforms=THREE.UniformsUtils.clone(this.uniforms);a.attributes=this.attributes;a.defines=this.defines;a.shading=this.shading;a.wireframe=this.wireframe;a.wireframeLinewidth=this.wireframeLinewidth;a.fog=this.fog;a.lights=this.lights;a.vertexColors=this.vertexColors;a.skinning=this.skinning;a.morphTargets="
                << std::endl ;
            out
                << "this.morphTargets;a.morphNormals=this.morphNormals;return a};THREE.RawShaderMaterial=function(a){THREE.ShaderMaterial.call(this,a);this.type=\"RawShaderMaterial\"};THREE.RawShaderMaterial.prototype=Object.create(THREE.ShaderMaterial.prototype);THREE.RawShaderMaterial.prototype.constructor=THREE.RawShaderMaterial;THREE.RawShaderMaterial.prototype.clone=function(){var a=new THREE.RawShaderMaterial;THREE.ShaderMaterial.prototype.clone.call(this,a);return a};"
                << std::endl ;
            out
                << "THREE.SpriteMaterial=function(a){THREE.Material.call(this);this.type=\"SpriteMaterial\";this.color=new THREE.Color(16777215);this.map=null;this.rotation=0;this.fog=!1;this.setValues(a)};THREE.SpriteMaterial.prototype=Object.create(THREE.Material.prototype);THREE.SpriteMaterial.prototype.constructor=THREE.SpriteMaterial;"
                << std::endl ;
            out
                << "THREE.SpriteMaterial.prototype.clone=function(){var a=new THREE.SpriteMaterial;THREE.Material.prototype.clone.call(this,a);a.color.copy(this.color);a.map=this.map;a.rotation=this.rotation;a.fog=this.fog;return a};"
                << std::endl ;
            out
                << "THREE.Texture=function(a,b,c,d,e,f,g,h,k){Object.defineProperty(this,\"id\",{value:THREE.TextureIdCount++});this.uuid=THREE.Math.generateUUID();this.sourceFile=this.name=\"\";this.image=void 0!==a?a:THREE.Texture.DEFAULT_IMAGE;this.mipmaps=[];this.mapping=void 0!==b?b:THREE.Texture.DEFAULT_MAPPING;this.wrapS=void 0!==c?c:THREE.ClampToEdgeWrapping;this.wrapT=void 0!==d?d:THREE.ClampToEdgeWrapping;this.magFilter=void 0!==e?e:THREE.LinearFilter;this.minFilter=void 0!==f?f:THREE.LinearMipMapLinearFilter;"
                << std::endl ;
            out
                << "this.anisotropy=void 0!==k?k:1;this.format=void 0!==g?g:THREE.RGBAFormat;this.type=void 0!==h?h:THREE.UnsignedByteType;this.offset=new THREE.Vector2(0,0);this.repeat=new THREE.Vector2(1,1);this.generateMipmaps=!0;this.premultiplyAlpha=!1;this.flipY=!0;this.unpackAlignment=4;this._needsUpdate=!1;this.onUpdate=null};THREE.Texture.DEFAULT_IMAGE=void 0;THREE.Texture.DEFAULT_MAPPING=THREE.UVMapping;"
                << std::endl ;
            out
                << "THREE.Texture.prototype={constructor:THREE.Texture,get needsUpdate(){return this._needsUpdate},set needsUpdate(a){!0===a&&this.update();this._needsUpdate=a},clone:function(a){void 0===a&&(a=new THREE.Texture);a.image=this.image;a.mipmaps=this.mipmaps.slice(0);a.mapping=this.mapping;a.wrapS=this.wrapS;a.wrapT=this.wrapT;a.magFilter=this.magFilter;a.minFilter=this.minFilter;a.anisotropy=this.anisotropy;a.format=this.format;a.type=this.type;a.offset.copy(this.offset);a.repeat.copy(this.repeat);a.generateMipmaps="
                << std::endl ;
            out
                << "this.generateMipmaps;a.premultiplyAlpha=this.premultiplyAlpha;a.flipY=this.flipY;a.unpackAlignment=this.unpackAlignment;return a},update:function(){this.dispatchEvent({type:\"update\"})},dispose:function(){this.dispatchEvent({type:\"dispose\"})}};THREE.EventDispatcher.prototype.apply(THREE.Texture.prototype);THREE.TextureIdCount=0;THREE.CubeTexture=function(a,b,c,d,e,f,g,h,k){b=void 0!==b?b:THREE.CubeReflectionMapping;THREE.Texture.call(this,a,b,c,d,e,f,g,h,k);this.images=a};"
                << std::endl ;
            out
                << "THREE.CubeTexture.prototype=Object.create(THREE.Texture.prototype);THREE.CubeTexture.prototype.constructor=THREE.CubeTexture;THREE.CubeTexture.clone=function(a){void 0===a&&(a=new THREE.CubeTexture);THREE.Texture.prototype.clone.call(this,a);a.images=this.images;return a};THREE.CompressedTexture=function(a,b,c,d,e,f,g,h,k,l,p){THREE.Texture.call(this,null,f,g,h,k,l,d,e,p);this.image={width:b,height:c};this.mipmaps=a;this.generateMipmaps=this.flipY=!1};THREE.CompressedTexture.prototype=Object.create(THREE.Texture.prototype);"
                << std::endl ;
            out
                << "THREE.CompressedTexture.prototype.constructor=THREE.CompressedTexture;THREE.CompressedTexture.prototype.clone=function(){var a=new THREE.CompressedTexture;THREE.Texture.prototype.clone.call(this,a);return a};THREE.DataTexture=function(a,b,c,d,e,f,g,h,k,l,p){THREE.Texture.call(this,null,f,g,h,k,l,d,e,p);this.image={data:a,width:b,height:c}};THREE.DataTexture.prototype=Object.create(THREE.Texture.prototype);THREE.DataTexture.prototype.constructor=THREE.DataTexture;"
                << std::endl ;
            out
                << "THREE.DataTexture.prototype.clone=function(){var a=new THREE.DataTexture;THREE.Texture.prototype.clone.call(this,a);return a};THREE.VideoTexture=function(a,b,c,d,e,f,g,h,k){THREE.Texture.call(this,a,b,c,d,e,f,g,h,k);this.generateMipmaps=!1;var l=this,p=function(){requestAnimationFrame(p);a.readyState===a.HAVE_ENOUGH_DATA&&(l.needsUpdate=!0)};p()};THREE.VideoTexture.prototype=Object.create(THREE.Texture.prototype);THREE.VideoTexture.prototype.constructor=THREE.VideoTexture;"
                << std::endl ;
            out
                << "THREE.Group=function(){THREE.Object3D.call(this);this.type=\"Group\"};THREE.Group.prototype=Object.create(THREE.Object3D.prototype);THREE.Group.prototype.constructor=THREE.Group;THREE.PointCloud=function(a,b){THREE.Object3D.call(this);this.type=\"PointCloud\";this.geometry=void 0!==a?a:new THREE.Geometry;this.material=void 0!==b?b:new THREE.PointCloudMaterial({color:16777215*Math.random()})};THREE.PointCloud.prototype=Object.create(THREE.Object3D.prototype);THREE.PointCloud.prototype.constructor=THREE.PointCloud;"
                << std::endl ;
            out
                << "THREE.PointCloud.prototype.raycast=function(){var a=new THREE.Matrix4,b=new THREE.Ray;return function(c,d){var e=this,f=e.geometry,g=c.params.PointCloud.threshold;a.getInverse(this.matrixWorld);b.copy(c.ray).applyMatrix4(a);if(null===f.boundingBox||!1!==b.isIntersectionBox(f.boundingBox)){var h=g/((this.scale.x+this.scale.y+this.scale.z)/3),k=new THREE.Vector3,g=function(a,f){var g=b.distanceToPoint(a);if(g<h){var k=b.closestPointToPoint(a);k.applyMatrix4(e.matrixWorld);var n=c.ray.origin.distanceTo(k);"
                << std::endl ;
            out
                << "d.push({distance:n,distanceToRay:g,point:k.clone(),index:f,face:null,object:e})}};if(f instanceof THREE.BufferGeometry){var l=f.attributes,p=l.position.array;if(void 0!==l.index){var l=l.index.array,q=f.offsets;0===q.length&&(q=[{start:0,count:l.length,index:0}]);for(var n=0,t=q.length;n<t;++n)for(var r=q[n].start,s=q[n].index,f=r,r=r+q[n].count;f<r;f++){var u=s+l[f];k.fromArray(p,3*u);g(k,u)}}else for(l=p.length/3,f=0;f<l;f++)k.set(p[3*f],p[3*f+1],p[3*f+2]),g(k,f)}else for(k=this.geometry.vertices,"
                << std::endl ;
            out
                << "f=0;f<k.length;f++)g(k[f],f)}}}();THREE.PointCloud.prototype.clone=function(a){void 0===a&&(a=new THREE.PointCloud(this.geometry,this.material));THREE.Object3D.prototype.clone.call(this,a);return a};THREE.ParticleSystem=function(a,b){THREE.warn(\"THREE.ParticleSystem has been renamed to THREE.PointCloud.\");return new THREE.PointCloud(a,b)};"
                << std::endl ;
            out
                << "THREE.Line=function(a,b,c){THREE.Object3D.call(this);this.type=\"Line\";this.geometry=void 0!==a?a:new THREE.Geometry;this.material=void 0!==b?b:new THREE.LineBasicMaterial({color:16777215*Math.random()});this.mode=void 0!==c?c:THREE.LineStrip};THREE.LineStrip=0;THREE.LinePieces=1;THREE.Line.prototype=Object.create(THREE.Object3D.prototype);THREE.Line.prototype.constructor=THREE.Line;"
                << std::endl ;
            out
                << "THREE.Line.prototype.raycast=function(){var a=new THREE.Matrix4,b=new THREE.Ray,c=new THREE.Sphere;return function(d,e){var f=d.linePrecision,f=f*f,g=this.geometry;null===g.boundingSphere&&g.computeBoundingSphere();c.copy(g.boundingSphere);c.applyMatrix4(this.matrixWorld);if(!1!==d.ray.isIntersectionSphere(c)){a.getInverse(this.matrixWorld);b.copy(d.ray).applyMatrix4(a);var h=new THREE.Vector3,k=new THREE.Vector3,l=new THREE.Vector3,p=new THREE.Vector3,q=this.mode===THREE.LineStrip?1:2;if(g instanceof"
                << std::endl ;
            out
                << "THREE.BufferGeometry){var n=g.attributes;if(void 0!==n.index){var t=n.index.array,n=n.position.array,r=g.offsets;0===r.length&&(r=[{start:0,count:t.length,index:0}]);for(var s=0;s<r.length;s++)for(var u=r[s].start,v=r[s].count,x=r[s].index,g=u;g<u+v-1;g+=q){var D=x+t[g+1];h.fromArray(n,3*(x+t[g]));k.fromArray(n,3*D);D=b.distanceSqToSegment(h,k,p,l);D>f||(D=b.origin.distanceTo(p),D<d.near||D>d.far||e.push({distance:D,point:l.clone().applyMatrix4(this.matrixWorld),index:g,offsetIndex:s,face:null,faceIndex:null,"
                << std::endl ;
            out
                << "object:this}))}}else for(n=n.position.array,g=0;g<n.length/3-1;g+=q)h.fromArray(n,3*g),k.fromArray(n,3*g+3),D=b.distanceSqToSegment(h,k,p,l),D>f||(D=b.origin.distanceTo(p),D<d.near||D>d.far||e.push({distance:D,point:l.clone().applyMatrix4(this.matrixWorld),index:g,face:null,faceIndex:null,object:this}))}else if(g instanceof THREE.Geometry)for(h=g.vertices,k=h.length,g=0;g<k-1;g+=q)D=b.distanceSqToSegment(h[g],h[g+1],p,l),D>f||(D=b.origin.distanceTo(p),D<d.near||D>d.far||e.push({distance:D,point:l.clone().applyMatrix4(this.matrixWorld),"
                << std::endl ;
            out
                << "index:g,face:null,faceIndex:null,object:this}))}}}();THREE.Line.prototype.clone=function(a){void 0===a&&(a=new THREE.Line(this.geometry,this.material,this.mode));THREE.Object3D.prototype.clone.call(this,a);return a};THREE.Mesh=function(a,b){THREE.Object3D.call(this);this.type=\"Mesh\";this.geometry=void 0!==a?a:new THREE.Geometry;this.material=void 0!==b?b:new THREE.MeshBasicMaterial({color:16777215*Math.random()});this.updateMorphTargets()};THREE.Mesh.prototype=Object.create(THREE.Object3D.prototype);"
                << std::endl ;
            out
                << "THREE.Mesh.prototype.constructor=THREE.Mesh;THREE.Mesh.prototype.updateMorphTargets=function(){if(void 0!==this.geometry.morphTargets&&0<this.geometry.morphTargets.length){this.morphTargetBase=-1;this.morphTargetForcedOrder=[];this.morphTargetInfluences=[];this.morphTargetDictionary={};for(var a=0,b=this.geometry.morphTargets.length;a<b;a++)this.morphTargetInfluences.push(0),this.morphTargetDictionary[this.geometry.morphTargets[a].name]=a}};"
                << std::endl ;
            out
                << "THREE.Mesh.prototype.getMorphTargetIndexByName=function(a){if(void 0!==this.morphTargetDictionary[a])return this.morphTargetDictionary[a];THREE.warn(\"THREE.Mesh.getMorphTargetIndexByName: morph target \"+a+\" does not exist. Returning 0.\");return 0};"
                << std::endl ;
            out
                << "THREE.Mesh.prototype.raycast=function(){var a=new THREE.Matrix4,b=new THREE.Ray,c=new THREE.Sphere,d=new THREE.Vector3,e=new THREE.Vector3,f=new THREE.Vector3;return function(g,h){var k=this.geometry;null===k.boundingSphere&&k.computeBoundingSphere();c.copy(k.boundingSphere);c.applyMatrix4(this.matrixWorld);if(!1!==g.ray.isIntersectionSphere(c)&&(a.getInverse(this.matrixWorld),b.copy(g.ray).applyMatrix4(a),null===k.boundingBox||!1!==b.isIntersectionBox(k.boundingBox)))if(k instanceof THREE.BufferGeometry){var l="
                << std::endl ;
            out
                << "this.material;if(void 0!==l){var p=k.attributes,q,n,t=g.precision;if(void 0!==p.index){var r=p.index.array,s=p.position.array,u=k.offsets;0===u.length&&(u=[{start:0,count:r.length,index:0}]);for(var v=0,x=u.length;v<x;++v)for(var p=u[v].start,D=u[v].index,k=p,w=p+u[v].count;k<w;k+=3){p=D+r[k];q=D+r[k+1];n=D+r[k+2];d.fromArray(s,3*p);e.fromArray(s,3*q);f.fromArray(s,3*n);var y=l.side===THREE.BackSide?b.intersectTriangle(f,e,d,!0):b.intersectTriangle(d,e,f,l.side!==THREE.DoubleSide);if(null!==y){y.applyMatrix4(this.matrixWorld);"
                << std::endl ;
            out
                << "var A=g.ray.origin.distanceTo(y);A<t||A<g.near||A>g.far||h.push({distance:A,point:y,face:new THREE.Face3(p,q,n,THREE.Triangle.normal(d,e,f)),faceIndex:null,object:this})}}}else for(s=p.position.array,r=k=0,w=s.length;k<w;k+=3,r+=9)p=k,q=k+1,n=k+2,d.fromArray(s,r),e.fromArray(s,r+3),f.fromArray(s,r+6),y=l.side===THREE.BackSide?b.intersectTriangle(f,e,d,!0):b.intersectTriangle(d,e,f,l.side!==THREE.DoubleSide),null!==y&&(y.applyMatrix4(this.matrixWorld),A=g.ray.origin.distanceTo(y),A<t||A<g.near||A>"
                << std::endl ;
            out
                << "g.far||h.push({distance:A,point:y,face:new THREE.Face3(p,q,n,THREE.Triangle.normal(d,e,f)),faceIndex:null,object:this}))}}else if(k instanceof THREE.Geometry)for(r=this.material instanceof THREE.MeshFaceMaterial,s=!0===r?this.material.materials:null,t=g.precision,u=k.vertices,v=0,x=k.faces.length;v<x;v++)if(D=k.faces[v],l=!0===r?s[D.materialIndex]:this.material,void 0!==l){p=u[D.a];q=u[D.b];n=u[D.c];if(!0===l.morphTargets){y=k.morphTargets;A=this.morphTargetInfluences;d.set(0,0,0);e.set(0,0,0);f.set(0,"
                << std::endl ;
            out
                << "0,0);for(var w=0,E=y.length;w<E;w++){var G=A[w];if(0!==G){var F=y[w].vertices;d.x+=(F[D.a].x-p.x)*G;d.y+=(F[D.a].y-p.y)*G;d.z+=(F[D.a].z-p.z)*G;e.x+=(F[D.b].x-q.x)*G;e.y+=(F[D.b].y-q.y)*G;e.z+=(F[D.b].z-q.z)*G;f.x+=(F[D.c].x-n.x)*G;f.y+=(F[D.c].y-n.y)*G;f.z+=(F[D.c].z-n.z)*G}}d.add(p);e.add(q);f.add(n);p=d;q=e;n=f}y=l.side===THREE.BackSide?b.intersectTriangle(n,q,p,!0):b.intersectTriangle(p,q,n,l.side!==THREE.DoubleSide);null!==y&&(y.applyMatrix4(this.matrixWorld),A=g.ray.origin.distanceTo(y),A<t||"
                << std::endl ;
            out
                << "A<g.near||A>g.far||h.push({distance:A,point:y,face:D,faceIndex:v,object:this}))}}}();THREE.Mesh.prototype.clone=function(a,b){void 0===a&&(a=new THREE.Mesh(this.geometry,this.material));THREE.Object3D.prototype.clone.call(this,a,b);return a};THREE.Bone=function(a){THREE.Object3D.call(this);this.type=\"Bone\";this.skin=a};THREE.Bone.prototype=Object.create(THREE.Object3D.prototype);THREE.Bone.prototype.constructor=THREE.Bone;"
                << std::endl ;
            out
                << "THREE.Skeleton=function(a,b,c){this.useVertexTexture=void 0!==c?c:!0;this.identityMatrix=new THREE.Matrix4;a=a||[];this.bones=a.slice(0);this.useVertexTexture?(this.boneTextureHeight=this.boneTextureWidth=a=256<this.bones.length?64:64<this.bones.length?32:16<this.bones.length?16:8,this.boneMatrices=new Float32Array(this.boneTextureWidth*this.boneTextureHeight*4),this.boneTexture=new THREE.DataTexture(this.boneMatrices,this.boneTextureWidth,this.boneTextureHeight,THREE.RGBAFormat,THREE.FloatType),"
                << std::endl ;
            out
                << "this.boneTexture.minFilter=THREE.NearestFilter,this.boneTexture.magFilter=THREE.NearestFilter,this.boneTexture.generateMipmaps=!1,this.boneTexture.flipY=!1):this.boneMatrices=new Float32Array(16*this.bones.length);if(void 0===b)this.calculateInverses();else if(this.bones.length===b.length)this.boneInverses=b.slice(0);else for(THREE.warn(\"THREE.Skeleton bonInverses is the wrong length.\"),this.boneInverses=[],b=0,a=this.bones.length;b<a;b++)this.boneInverses.push(new THREE.Matrix4)};"
                << std::endl ;
            out
                << "THREE.Skeleton.prototype.calculateInverses=function(){this.boneInverses=[];for(var a=0,b=this.bones.length;a<b;a++){var c=new THREE.Matrix4;this.bones[a]&&c.getInverse(this.bones[a].matrixWorld);this.boneInverses.push(c)}};"
                << std::endl ;
            out
                << "THREE.Skeleton.prototype.pose=function(){for(var a,b=0,c=this.bones.length;b<c;b++)(a=this.bones[b])&&a.matrixWorld.getInverse(this.boneInverses[b]);b=0;for(c=this.bones.length;b<c;b++)if(a=this.bones[b])a.parent?(a.matrix.getInverse(a.parent.matrixWorld),a.matrix.multiply(a.matrixWorld)):a.matrix.copy(a.matrixWorld),a.matrix.decompose(a.position,a.quaternion,a.scale)};"
                << std::endl ;
            out
                << "THREE.Skeleton.prototype.update=function(){var a=new THREE.Matrix4;return function(){for(var b=0,c=this.bones.length;b<c;b++)a.multiplyMatrices(this.bones[b]?this.bones[b].matrixWorld:this.identityMatrix,this.boneInverses[b]),a.flattenToArrayOffset(this.boneMatrices,16*b);this.useVertexTexture&&(this.boneTexture.needsUpdate=!0)}}();"
                << std::endl ;
            out
                << "THREE.SkinnedMesh=function(a,b,c){THREE.Mesh.call(this,a,b);this.type=\"SkinnedMesh\";this.bindMode=\"attached\";this.bindMatrix=new THREE.Matrix4;this.bindMatrixInverse=new THREE.Matrix4;a=[];if(this.geometry&&void 0!==this.geometry.bones){for(var d,e,f,g,h=0,k=this.geometry.bones.length;h<k;++h)d=this.geometry.bones[h],e=d.pos,f=d.rotq,g=d.scl,b=new THREE.Bone(this),a.push(b),b.name=d.name,b.position.set(e[0],e[1],e[2]),b.quaternion.set(f[0],f[1],f[2],f[3]),void 0!==g?b.scale.set(g[0],g[1],g[2]):b.scale.set(1,"
                << std::endl ;
            out
                << "1,1);h=0;for(k=this.geometry.bones.length;h<k;++h)d=this.geometry.bones[h],-1!==d.parent?a[d.parent].add(a[h]):this.add(a[h])}this.normalizeSkinWeights();this.updateMatrixWorld(!0);this.bind(new THREE.Skeleton(a,void 0,c))};THREE.SkinnedMesh.prototype=Object.create(THREE.Mesh.prototype);THREE.SkinnedMesh.prototype.constructor=THREE.SkinnedMesh;THREE.SkinnedMesh.prototype.bind=function(a,b){this.skeleton=a;void 0===b&&(this.updateMatrixWorld(!0),b=this.matrixWorld);this.bindMatrix.copy(b);this.bindMatrixInverse.getInverse(b)};"
                << std::endl ;
            out
                << "THREE.SkinnedMesh.prototype.pose=function(){this.skeleton.pose()};THREE.SkinnedMesh.prototype.normalizeSkinWeights=function(){if(this.geometry instanceof THREE.Geometry)for(var a=0;a<this.geometry.skinIndices.length;a++){var b=this.geometry.skinWeights[a],c=1/b.lengthManhattan();Infinity!==c?b.multiplyScalar(c):b.set(1)}};"
                << std::endl ;
            out
                << "THREE.SkinnedMesh.prototype.updateMatrixWorld=function(a){THREE.Mesh.prototype.updateMatrixWorld.call(this,!0);\"attached\"===this.bindMode?this.bindMatrixInverse.getInverse(this.matrixWorld):\"detached\"===this.bindMode?this.bindMatrixInverse.getInverse(this.bindMatrix):THREE.warn(\"THREE.SkinnedMesh unreckognized bindMode: \"+this.bindMode)};"
                << std::endl ;
            out
                << "THREE.SkinnedMesh.prototype.clone=function(a){void 0===a&&(a=new THREE.SkinnedMesh(this.geometry,this.material,this.useVertexTexture));THREE.Mesh.prototype.clone.call(this,a);return a};THREE.MorphAnimMesh=function(a,b){THREE.Mesh.call(this,a,b);this.type=\"MorphAnimMesh\";this.duration=1E3;this.mirroredLoop=!1;this.currentKeyframe=this.lastKeyframe=this.time=0;this.direction=1;this.directionBackwards=!1;this.setFrameRange(0,this.geometry.morphTargets.length-1)};THREE.MorphAnimMesh.prototype=Object.create(THREE.Mesh.prototype);"
                << std::endl ;
            out
                << "THREE.MorphAnimMesh.prototype.constructor=THREE.MorphAnimMesh;THREE.MorphAnimMesh.prototype.setFrameRange=function(a,b){this.startKeyframe=a;this.endKeyframe=b;this.length=this.endKeyframe-this.startKeyframe+1};THREE.MorphAnimMesh.prototype.setDirectionForward=function(){this.direction=1;this.directionBackwards=!1};THREE.MorphAnimMesh.prototype.setDirectionBackward=function(){this.direction=-1;this.directionBackwards=!0};"
                << std::endl ;
            out
                << "THREE.MorphAnimMesh.prototype.parseAnimations=function(){var a=this.geometry;a.animations||(a.animations={});for(var b,c=a.animations,d=/([a-z]+)_?(\\d+)/,e=0,f=a.morphTargets.length;e<f;e++){var g=a.morphTargets[e].name.match(d);if(g&&1<g.length){g=g[1];c[g]||(c[g]={start:Infinity,end:-Infinity});var h=c[g];e<h.start&&(h.start=e);e>h.end&&(h.end=e);b||(b=g)}}a.firstAnimation=b};"
                << std::endl ;
            out
                << "THREE.MorphAnimMesh.prototype.setAnimationLabel=function(a,b,c){this.geometry.animations||(this.geometry.animations={});this.geometry.animations[a]={start:b,end:c}};THREE.MorphAnimMesh.prototype.playAnimation=function(a,b){var c=this.geometry.animations[a];c?(this.setFrameRange(c.start,c.end),this.duration=(c.end-c.start)/b*1E3,this.time=0):THREE.warn(\"THREE.MorphAnimMesh: animation[\"+a+\"] undefined in .playAnimation()\")};"
                << std::endl ;
            out
                << "THREE.MorphAnimMesh.prototype.updateAnimation=function(a){var b=this.duration/this.length;this.time+=this.direction*a;if(this.mirroredLoop){if(this.time>this.duration||0>this.time)this.direction*=-1,this.time>this.duration&&(this.time=this.duration,this.directionBackwards=!0),0>this.time&&(this.time=0,this.directionBackwards=!1)}else this.time%=this.duration,0>this.time&&(this.time+=this.duration);a=this.startKeyframe+THREE.Math.clamp(Math.floor(this.time/b),0,this.length-1);a!==this.currentKeyframe&&"
                << std::endl ;
            out
                << "(this.morphTargetInfluences[this.lastKeyframe]=0,this.morphTargetInfluences[this.currentKeyframe]=1,this.morphTargetInfluences[a]=0,this.lastKeyframe=this.currentKeyframe,this.currentKeyframe=a);b=this.time%b/b;this.directionBackwards&&(b=1-b);this.morphTargetInfluences[this.currentKeyframe]=b;this.morphTargetInfluences[this.lastKeyframe]=1-b};"
                << std::endl ;
            out
                << "THREE.MorphAnimMesh.prototype.interpolateTargets=function(a,b,c){for(var d=this.morphTargetInfluences,e=0,f=d.length;e<f;e++)d[e]=0;-1<a&&(d[a]=1-c);-1<b&&(d[b]=c)};"
                << std::endl ;
            out
                << "THREE.MorphAnimMesh.prototype.clone=function(a){void 0===a&&(a=new THREE.MorphAnimMesh(this.geometry,this.material));a.duration=this.duration;a.mirroredLoop=this.mirroredLoop;a.time=this.time;a.lastKeyframe=this.lastKeyframe;a.currentKeyframe=this.currentKeyframe;a.direction=this.direction;a.directionBackwards=this.directionBackwards;THREE.Mesh.prototype.clone.call(this,a);return a};THREE.LOD=function(){THREE.Object3D.call(this);this.objects=[]};THREE.LOD.prototype=Object.create(THREE.Object3D.prototype);"
                << std::endl ;
            out
                << "THREE.LOD.prototype.constructor=THREE.LOD;THREE.LOD.prototype.addLevel=function(a,b){void 0===b&&(b=0);b=Math.abs(b);for(var c=0;c<this.objects.length&&!(b<this.objects[c].distance);c++);this.objects.splice(c,0,{distance:b,object:a});this.add(a)};THREE.LOD.prototype.getObjectForDistance=function(a){for(var b=1,c=this.objects.length;b<c&&!(a<this.objects[b].distance);b++);return this.objects[b-1].object};"
                << std::endl ;
            out
                << "THREE.LOD.prototype.raycast=function(){var a=new THREE.Vector3;return function(b,c){a.setFromMatrixPosition(this.matrixWorld);var d=b.ray.origin.distanceTo(a);this.getObjectForDistance(d).raycast(b,c)}}();"
                << std::endl ;
            out
                << "THREE.LOD.prototype.update=function(){var a=new THREE.Vector3,b=new THREE.Vector3;return function(c){if(1<this.objects.length){a.setFromMatrixPosition(c.matrixWorld);b.setFromMatrixPosition(this.matrixWorld);c=a.distanceTo(b);this.objects[0].object.visible=!0;for(var d=1,e=this.objects.length;d<e;d++)if(c>=this.objects[d].distance)this.objects[d-1].object.visible=!1,this.objects[d].object.visible=!0;else break;for(;d<e;d++)this.objects[d].object.visible=!1}}}();"
                << std::endl ;
            out
                << "THREE.LOD.prototype.clone=function(a){void 0===a&&(a=new THREE.LOD);THREE.Object3D.prototype.clone.call(this,a);for(var b=0,c=this.objects.length;b<c;b++){var d=this.objects[b].object.clone();d.visible=0===b;a.addLevel(d,this.objects[b].distance)}return a};"
                << std::endl ;
            out
                << "THREE.Sprite=function(){var a=new Uint16Array([0,1,2,0,2,3]),b=new Float32Array([-.5,-.5,0,.5,-.5,0,.5,.5,0,-.5,.5,0]),c=new Float32Array([0,0,1,0,1,1,0,1]),d=new THREE.BufferGeometry;d.addAttribute(\"index\",new THREE.BufferAttribute(a,1));d.addAttribute(\"position\",new THREE.BufferAttribute(b,3));d.addAttribute(\"uv\",new THREE.BufferAttribute(c,2));return function(a){THREE.Object3D.call(this);this.type=\"Sprite\";this.geometry=d;this.material=void 0!==a?a:new THREE.SpriteMaterial}}();"
                << std::endl ;
            out
                << "THREE.Sprite.prototype=Object.create(THREE.Object3D.prototype);THREE.Sprite.prototype.constructor=THREE.Sprite;THREE.Sprite.prototype.raycast=function(){var a=new THREE.Vector3;return function(b,c){a.setFromMatrixPosition(this.matrixWorld);var d=b.ray.distanceToPoint(a);d>this.scale.x||c.push({distance:d,point:this.position,face:null,object:this})}}();THREE.Sprite.prototype.clone=function(a){void 0===a&&(a=new THREE.Sprite(this.material));THREE.Object3D.prototype.clone.call(this,a);return a};"
                << std::endl ;
            out
                << "THREE.Particle=THREE.Sprite;THREE.LensFlare=function(a,b,c,d,e){THREE.Object3D.call(this);this.lensFlares=[];this.positionScreen=new THREE.Vector3;this.customUpdateCallback=void 0;void 0!==a&&this.add(a,b,c,d,e)};THREE.LensFlare.prototype=Object.create(THREE.Object3D.prototype);THREE.LensFlare.prototype.constructor=THREE.LensFlare;"
                << std::endl ;
            out
                << "THREE.LensFlare.prototype.add=function(a,b,c,d,e,f){void 0===b&&(b=-1);void 0===c&&(c=0);void 0===f&&(f=1);void 0===e&&(e=new THREE.Color(16777215));void 0===d&&(d=THREE.NormalBlending);c=Math.min(c,Math.max(0,c));this.lensFlares.push({texture:a,size:b,distance:c,x:0,y:0,z:0,scale:1,rotation:1,opacity:f,color:e,blending:d})};"
                << std::endl ;
            out
                << "THREE.LensFlare.prototype.updateLensFlares=function(){var a,b=this.lensFlares.length,c,d=2*-this.positionScreen.x,e=2*-this.positionScreen.y;for(a=0;a<b;a++)c=this.lensFlares[a],c.x=this.positionScreen.x+d*c.distance,c.y=this.positionScreen.y+e*c.distance,c.wantedRotation=c.x*Math.PI*.25,c.rotation+=.25*(c.wantedRotation-c.rotation)};THREE.Scene=function(){THREE.Object3D.call(this);this.type=\"Scene\";this.overrideMaterial=this.fog=null;this.autoUpdate=!0};THREE.Scene.prototype=Object.create(THREE.Object3D.prototype);"
                << std::endl ;
            out
                << "THREE.Scene.prototype.constructor=THREE.Scene;THREE.Scene.prototype.clone=function(a){void 0===a&&(a=new THREE.Scene);THREE.Object3D.prototype.clone.call(this,a);null!==this.fog&&(a.fog=this.fog.clone());null!==this.overrideMaterial&&(a.overrideMaterial=this.overrideMaterial.clone());a.autoUpdate=this.autoUpdate;a.matrixAutoUpdate=this.matrixAutoUpdate;return a};THREE.Fog=function(a,b,c){this.name=\"\";this.color=new THREE.Color(a);this.near=void 0!==b?b:1;this.far=void 0!==c?c:1E3};"
                << std::endl ;
            out
                << "THREE.Fog.prototype.clone=function(){return new THREE.Fog(this.color.getHex(),this.near,this.far)};THREE.FogExp2=function(a,b){this.name=\"\";this.color=new THREE.Color(a);this.density=void 0!==b?b:2.5E-4};THREE.FogExp2.prototype.clone=function(){return new THREE.FogExp2(this.color.getHex(),this.density)};THREE.ShaderChunk={};THREE.ShaderChunk.common=\"#define PI 3.14159\\n#define PI2 6.28318\\n#define RECIPROCAL_PI2 0.15915494\\n#define LOG2 1.442695\\n#define EPSILON 1e-6\\n\\nfloat square( in float a ) { return a*a; }\\nvec2  square( in vec2 a )  { return vec2( a.x*a.x, a.y*a.y ); }\\nvec3  square( in vec3 a )  { return vec3( a.x*a.x, a.y*a.y, a.z*a.z ); }\\nvec4  square( in vec4 a )  { return vec4( a.x*a.x, a.y*a.y, a.z*a.z, a.w*a.w ); }\\nfloat saturate( in float a ) { return clamp( a, 0.0, 1.0 ); }\\nvec2  saturate( in vec2 a )  { return clamp( a, 0.0, 1.0 ); }\\nvec3  saturate( in vec3 a )  { return clamp( a, 0.0, 1.0 ); }\\nvec4  saturate( in vec4 a )  { return clamp( a, 0.0, 1.0 ); }\\nfloat average( in float a ) { return a; }\\nfloat average( in vec2 a )  { return ( a.x + a.y) * 0.5; }\\nfloat average( in vec3 a )  { return ( a.x + a.y + a.z) / 3.0; }\\nfloat average( in vec4 a )  { return ( a.x + a.y + a.z + a.w) * 0.25; }\\nfloat whiteCompliment( in float a ) { return saturate( 1.0 - a ); }\\nvec2  whiteCompliment( in vec2 a )  { return saturate( vec2(1.0) - a ); }\\nvec3  whiteCompliment( in vec3 a )  { return saturate( vec3(1.0) - a ); }\\nvec4  whiteCompliment( in vec4 a )  { return saturate( vec4(1.0) - a ); }\\nvec3 transformDirection( in vec3 normal, in mat4 matrix ) {\\n\\treturn normalize( ( matrix * vec4( normal, 0.0 ) ).xyz );\\n}\\n// http://en.wikibooks.org/wiki/GLSL_Programming/Applying_Matrix_Transformations\\nvec3 inverseTransformDirection( in vec3 normal, in mat4 matrix ) {\\n\\treturn normalize( ( vec4( normal, 0.0 ) * matrix ).xyz );\\n}\\nvec3 projectOnPlane(in vec3 point, in vec3 pointOnPlane, in vec3 planeNormal) {\\n\\tfloat distance = dot( planeNormal, point-pointOnPlane );\\n\\treturn point - distance * planeNormal;\\n}\\nfloat sideOfPlane( in vec3 point, in vec3 pointOnPlane, in vec3 planeNormal ) {\\n\\treturn sign( dot( point - pointOnPlane, planeNormal ) );\\n}\\nvec3 linePlaneIntersect( in vec3 pointOnLine, in vec3 lineDirection, in vec3 pointOnPlane, in vec3 planeNormal ) {\\n\\treturn pointOnLine + lineDirection * ( dot( planeNormal, pointOnPlane - pointOnLine ) / dot( planeNormal, lineDirection ) );\\n}\\nfloat calcLightAttenuation( float lightDistance, float cutoffDistance, float decayExponent ) {\\n\\tif ( decayExponent > 0.0 ) {\\n\\t  return pow( saturate( 1.0 - lightDistance / cutoffDistance ), decayExponent );\\n\\t}\\n\\treturn 1.0;\\n}\\n\\nvec3 inputToLinear( in vec3 a ) {\\n#ifdef GAMMA_INPUT\\n\\treturn pow( a, vec3( float( GAMMA_FACTOR ) ) );\\n#else\\n\\treturn a;\\n#endif\\n}\\nvec3 linearToOutput( in vec3 a ) {\\n#ifdef GAMMA_OUTPUT\\n\\treturn pow( a, vec3( 1.0 / float( GAMMA_FACTOR ) ) );\\n#else\\n\\treturn a;\\n#endif\\n}\\n\";"
                << std::endl ;
            out
                << "THREE.ShaderChunk.alphatest_fragment=\"#ifdef ALPHATEST\\n\\n\\tif ( diffuseColor.a < ALPHATEST ) discard;\\n\\n#endif\\n\";THREE.ShaderChunk.lights_lambert_vertex=\"vLightFront = vec3( 0.0 );\\n\\n#ifdef DOUBLE_SIDED\\n\\n\\tvLightBack = vec3( 0.0 );\\n\\n#endif\\n\\ntransformedNormal = normalize( transformedNormal );\\n\\n#if MAX_DIR_LIGHTS > 0\\n\\nfor( int i = 0; i < MAX_DIR_LIGHTS; i ++ ) {\\n\\n\\tvec3 dirVector = transformDirection( directionalLightDirection[ i ], viewMatrix );\\n\\n\\tfloat dotProduct = dot( transformedNormal, dirVector );\\n\\tvec3 directionalLightWeighting = vec3( max( dotProduct, 0.0 ) );\\n\\n\\t#ifdef DOUBLE_SIDED\\n\\n\\t\\tvec3 directionalLightWeightingBack = vec3( max( -dotProduct, 0.0 ) );\\n\\n\\t\\t#ifdef WRAP_AROUND\\n\\n\\t\\t\\tvec3 directionalLightWeightingHalfBack = vec3( max( -0.5 * dotProduct + 0.5, 0.0 ) );\\n\\n\\t\\t#endif\\n\\n\\t#endif\\n\\n\\t#ifdef WRAP_AROUND\\n\\n\\t\\tvec3 directionalLightWeightingHalf = vec3( max( 0.5 * dotProduct + 0.5, 0.0 ) );\\n\\t\\tdirectionalLightWeighting = mix( directionalLightWeighting, directionalLightWeightingHalf, wrapRGB );\\n\\n\\t\\t#ifdef DOUBLE_SIDED\\n\\n\\t\\t\\tdirectionalLightWeightingBack = mix( directionalLightWeightingBack, directionalLightWeightingHalfBack, wrapRGB );\\n\\n\\t\\t#endif\\n\\n\\t#endif\\n\\n\\tvLightFront += directionalLightColor[ i ] * directionalLightWeighting;\\n\\n\\t#ifdef DOUBLE_SIDED\\n\\n\\t\\tvLightBack += directionalLightColor[ i ] * directionalLightWeightingBack;\\n\\n\\t#endif\\n\\n}\\n\\n#endif\\n\\n#if MAX_POINT_LIGHTS > 0\\n\\n\\tfor( int i = 0; i < MAX_POINT_LIGHTS; i ++ ) {\\n\\n\\t\\tvec4 lPosition = viewMatrix * vec4( pointLightPosition[ i ], 1.0 );\\n\\t\\tvec3 lVector = lPosition.xyz - mvPosition.xyz;\\n\\n\\t\\tfloat attenuation = calcLightAttenuation( length( lVector ), pointLightDistance[ i ], pointLightDecay[ i ] );\\n\\n\\t\\tlVector = normalize( lVector );\\n\\t\\tfloat dotProduct = dot( transformedNormal, lVector );\\n\\n\\t\\tvec3 pointLightWeighting = vec3( max( dotProduct, 0.0 ) );\\n\\n\\t\\t#ifdef DOUBLE_SIDED\\n\\n\\t\\t\\tvec3 pointLightWeightingBack = vec3( max( -dotProduct, 0.0 ) );\\n\\n\\t\\t\\t#ifdef WRAP_AROUND\\n\\n\\t\\t\\t\\tvec3 pointLightWeightingHalfBack = vec3( max( -0.5 * dotProduct + 0.5, 0.0 ) );\\n\\n\\t\\t\\t#endif\\n\\n\\t\\t#endif\\n\\n\\t\\t#ifdef WRAP_AROUND\\n\\n\\t\\t\\tvec3 pointLightWeightingHalf = vec3( max( 0.5 * dotProduct + 0.5, 0.0 ) );\\n\\t\\t\\tpointLightWeighting = mix( pointLightWeighting, pointLightWeightingHalf, wrapRGB );\\n\\n\\t\\t\\t#ifdef DOUBLE_SIDED\\n\\n\\t\\t\\t\\tpointLightWeightingBack = mix( pointLightWeightingBack, pointLightWeightingHalfBack, wrapRGB );\\n\\n\\t\\t\\t#endif\\n\\n\\t\\t#endif\\n\\n\\t\\tvLightFront += pointLightColor[ i ] * pointLightWeighting * attenuation;\\n\\n\\t\\t#ifdef DOUBLE_SIDED\\n\\n\\t\\t\\tvLightBack += pointLightColor[ i ] * pointLightWeightingBack * attenuation;\\n\\n\\t\\t#endif\\n\\n\\t}\\n\\n#endif\\n\\n#if MAX_SPOT_LIGHTS > 0\\n\\n\\tfor( int i = 0; i < MAX_SPOT_LIGHTS; i ++ ) {\\n\\n\\t\\tvec4 lPosition = viewMatrix * vec4( spotLightPosition[ i ], 1.0 );\\n\\t\\tvec3 lVector = lPosition.xyz - mvPosition.xyz;\\n\\n\\t\\tfloat spotEffect = dot( spotLightDirection[ i ], normalize( spotLightPosition[ i ] - worldPosition.xyz ) );\\n\\n\\t\\tif ( spotEffect > spotLightAngleCos[ i ] ) {\\n\\n\\t\\t\\tspotEffect = max( pow( max( spotEffect, 0.0 ), spotLightExponent[ i ] ), 0.0 );\\n\\n\\t\\t\\tfloat attenuation = calcLightAttenuation( length( lVector ), spotLightDistance[ i ], spotLightDecay[ i ] );\\n\\n\\t\\t\\tlVector = normalize( lVector );\\n\\n\\t\\t\\tfloat dotProduct = dot( transformedNormal, lVector );\\n\\t\\t\\tvec3 spotLightWeighting = vec3( max( dotProduct, 0.0 ) );\\n\\n\\t\\t\\t#ifdef DOUBLE_SIDED\\n\\n\\t\\t\\t\\tvec3 spotLightWeightingBack = vec3( max( -dotProduct, 0.0 ) );\\n\\n\\t\\t\\t\\t#ifdef WRAP_AROUND\\n\\n\\t\\t\\t\\t\\tvec3 spotLightWeightingHalfBack = vec3( max( -0.5 * dotProduct + 0.5, 0.0 ) );\\n\\n\\t\\t\\t\\t#endif\\n\\n\\t\\t\\t#endif\\n\\n\\t\\t\\t#ifdef WRAP_AROUND\\n\\n\\t\\t\\t\\tvec3 spotLightWeightingHalf = vec3( max( 0.5 * dotProduct + 0.5, 0.0 ) );\\n\\t\\t\\t\\tspotLightWeighting = mix( spotLightWeighting, spotLightWeightingHalf, wrapRGB );\\n\\n\\t\\t\\t\\t#ifdef DOUBLE_SIDED\\n\\n\\t\\t\\t\\t\\tspotLightWeightingBack = mix( spotLightWeightingBack, spotLightWeightingHalfBack, wrapRGB );\\n\\n\\t\\t\\t\\t#endif\\n\\n\\t\\t\\t#endif\\n\\n\\t\\t\\tvLightFront += spotLightColor[ i ] * spotLightWeighting * attenuation * spotEffect;\\n\\n\\t\\t\\t#ifdef DOUBLE_SIDED\\n\\n\\t\\t\\t\\tvLightBack += spotLightColor[ i ] * spotLightWeightingBack * attenuation * spotEffect;\\n\\n\\t\\t\\t#endif\\n\\n\\t\\t}\\n\\n\\t}\\n\\n#endif\\n\\n#if MAX_HEMI_LIGHTS > 0\\n\\n\\tfor( int i = 0; i < MAX_HEMI_LIGHTS; i ++ ) {\\n\\n\\t\\tvec3 lVector = transformDirection( hemisphereLightDirection[ i ], viewMatrix );\\n\\n\\t\\tfloat dotProduct = dot( transformedNormal, lVector );\\n\\n\\t\\tfloat hemiDiffuseWeight = 0.5 * dotProduct + 0.5;\\n\\t\\tfloat hemiDiffuseWeightBack = -0.5 * dotProduct + 0.5;\\n\\n\\t\\tvLightFront += mix( hemisphereLightGroundColor[ i ], hemisphereLightSkyColor[ i ], hemiDiffuseWeight );\\n\\n\\t\\t#ifdef DOUBLE_SIDED\\n\\n\\t\\t\\tvLightBack += mix( hemisphereLightGroundColor[ i ], hemisphereLightSkyColor[ i ], hemiDiffuseWeightBack );\\n\\n\\t\\t#endif\\n\\n\\t}\\n\\n#endif\\n\\nvLightFront += ambientLightColor;\\n\\n#ifdef DOUBLE_SIDED\\n\\n\\tvLightBack += ambientLightColor;\\n\\n#endif\\n\";"
                << std::endl ;
            out
                << "THREE.ShaderChunk.map_particle_pars_fragment=\"#ifdef USE_MAP\\n\\n\\tuniform vec4 offsetRepeat;\\n\\tuniform sampler2D map;\\n\\n#endif\\n\";THREE.ShaderChunk.default_vertex=\"#ifdef USE_SKINNING\\n\\n\\tvec4 mvPosition = modelViewMatrix * skinned;\\n\\n#elif defined( USE_MORPHTARGETS )\\n\\n\\tvec4 mvPosition = modelViewMatrix * vec4( morphed, 1.0 );\\n\\n#else\\n\\n\\tvec4 mvPosition = modelViewMatrix * vec4( position, 1.0 );\\n\\n#endif\\n\\ngl_Position = projectionMatrix * mvPosition;\\n\";"
                << std::endl ;
            out
                << "THREE.ShaderChunk.map_pars_fragment=\"#if defined( USE_MAP ) || defined( USE_BUMPMAP ) || defined( USE_NORMALMAP ) || defined( USE_SPECULARMAP ) || defined( USE_ALPHAMAP )\\n\\n\\tvarying vec2 vUv;\\n\\n#endif\\n\\n#ifdef USE_MAP\\n\\n\\tuniform sampler2D map;\\n\\n#endif\";THREE.ShaderChunk.skinnormal_vertex=\"#ifdef USE_SKINNING\\n\\n\\tmat4 skinMatrix = mat4( 0.0 );\\n\\tskinMatrix += skinWeight.x * boneMatX;\\n\\tskinMatrix += skinWeight.y * boneMatY;\\n\\tskinMatrix += skinWeight.z * boneMatZ;\\n\\tskinMatrix += skinWeight.w * boneMatW;\\n\\tskinMatrix  = bindMatrixInverse * skinMatrix * bindMatrix;\\n\\n\\t#ifdef USE_MORPHNORMALS\\n\\n\\tvec4 skinnedNormal = skinMatrix * vec4( morphedNormal, 0.0 );\\n\\n\\t#else\\n\\n\\tvec4 skinnedNormal = skinMatrix * vec4( normal, 0.0 );\\n\\n\\t#endif\\n\\n#endif\\n\";"
                << std::endl ;
            out
                << "THREE.ShaderChunk.logdepthbuf_pars_vertex=\"#ifdef USE_LOGDEPTHBUF\\n\\n\\t#ifdef USE_LOGDEPTHBUF_EXT\\n\\n\\t\\tvarying float vFragDepth;\\n\\n\\t#endif\\n\\n\\tuniform float logDepthBufFC;\\n\\n#endif\";THREE.ShaderChunk.lightmap_pars_vertex=\"#ifdef USE_LIGHTMAP\\n\\n\\tvarying vec2 vUv2;\\n\\n#endif\";THREE.ShaderChunk.lights_phong_fragment=\"#ifndef FLAT_SHADED\\n\\n\\tvec3 normal = normalize( vNormal );\\n\\n\\t#ifdef DOUBLE_SIDED\\n\\n\\t\\tnormal = normal * ( -1.0 + 2.0 * float( gl_FrontFacing ) );\\n\\n\\t#endif\\n\\n#else\\n\\n\\tvec3 fdx = dFdx( vViewPosition );\\n\\tvec3 fdy = dFdy( vViewPosition );\\n\\tvec3 normal = normalize( cross( fdx, fdy ) );\\n\\n#endif\\n\\nvec3 viewPosition = normalize( vViewPosition );\\n\\n#ifdef USE_NORMALMAP\\n\\n\\tnormal = perturbNormal2Arb( -vViewPosition, normal );\\n\\n#elif defined( USE_BUMPMAP )\\n\\n\\tnormal = perturbNormalArb( -vViewPosition, normal, dHdxy_fwd() );\\n\\n#endif\\n\\nvec3 totalDiffuseLight = vec3( 0.0 );\\nvec3 totalSpecularLight = vec3( 0.0 );\\n\\n#if MAX_POINT_LIGHTS > 0\\n\\n\\tfor ( int i = 0; i < MAX_POINT_LIGHTS; i ++ ) {\\n\\n\\t\\tvec4 lPosition = viewMatrix * vec4( pointLightPosition[ i ], 1.0 );\\n\\t\\tvec3 lVector = lPosition.xyz + vViewPosition.xyz;\\n\\n\\t\\tfloat attenuation = calcLightAttenuation( length( lVector ), pointLightDistance[ i ], pointLightDecay[ i ] );\\n\\n\\t\\tlVector = normalize( lVector );\\n\\n\\t\\t// diffuse\\n\\n\\t\\tfloat dotProduct = dot( normal, lVector );\\n\\n\\t\\t#ifdef WRAP_AROUND\\n\\n\\t\\t\\tfloat pointDiffuseWeightFull = max( dotProduct, 0.0 );\\n\\t\\t\\tfloat pointDiffuseWeightHalf = max( 0.5 * dotProduct + 0.5, 0.0 );\\n\\n\\t\\t\\tvec3 pointDiffuseWeight = mix( vec3( pointDiffuseWeightFull ), vec3( pointDiffuseWeightHalf ), wrapRGB );\\n\\n\\t\\t#else\\n\\n\\t\\t\\tfloat pointDiffuseWeight = max( dotProduct, 0.0 );\\n\\n\\t\\t#endif\\n\\n\\t\\ttotalDiffuseLight += pointLightColor[ i ] * pointDiffuseWeight * attenuation;\\n\\n\\t\\t\\t\\t// specular\\n\\n\\t\\tvec3 pointHalfVector = normalize( lVector + viewPosition );\\n\\t\\tfloat pointDotNormalHalf = max( dot( normal, pointHalfVector ), 0.0 );\\n\\t\\tfloat pointSpecularWeight = specularStrength * max( pow( pointDotNormalHalf, shininess ), 0.0 );\\n\\n\\t\\tfloat specularNormalization = ( shininess + 2.0 ) / 8.0;\\n\\n\\t\\tvec3 schlick = specular + vec3( 1.0 - specular ) * pow( max( 1.0 - dot( lVector, pointHalfVector ), 0.0 ), 5.0 );\\n\\t\\ttotalSpecularLight += schlick * pointLightColor[ i ] * pointSpecularWeight * pointDiffuseWeight * attenuation * specularNormalization;\\n\\n\\t}\\n\\n#endif\\n\\n#if MAX_SPOT_LIGHTS > 0\\n\\n\\tfor ( int i = 0; i < MAX_SPOT_LIGHTS; i ++ ) {\\n\\n\\t\\tvec4 lPosition = viewMatrix * vec4( spotLightPosition[ i ], 1.0 );\\n\\t\\tvec3 lVector = lPosition.xyz + vViewPosition.xyz;\\n\\n\\t\\tfloat attenuation = calcLightAttenuation( length( lVector ), spotLightDistance[ i ], spotLightDecay[ i ] );\\n\\n\\t\\tlVector = normalize( lVector );\\n\\n\\t\\tfloat spotEffect = dot( spotLightDirection[ i ], normalize( spotLightPosition[ i ] - vWorldPosition ) );\\n\\n\\t\\tif ( spotEffect > spotLightAngleCos[ i ] ) {\\n\\n\\t\\t\\tspotEffect = max( pow( max( spotEffect, 0.0 ), spotLightExponent[ i ] ), 0.0 );\\n\\n\\t\\t\\t// diffuse\\n\\n\\t\\t\\tfloat dotProduct = dot( normal, lVector );\\n\\n\\t\\t\\t#ifdef WRAP_AROUND\\n\\n\\t\\t\\t\\tfloat spotDiffuseWeightFull = max( dotProduct, 0.0 );\\n\\t\\t\\t\\tfloat spotDiffuseWeightHalf = max( 0.5 * dotProduct + 0.5, 0.0 );\\n\\n\\t\\t\\t\\tvec3 spotDiffuseWeight = mix( vec3( spotDiffuseWeightFull ), vec3( spotDiffuseWeightHalf ), wrapRGB );\\n\\n\\t\\t\\t#else\\n\\n\\t\\t\\t\\tfloat spotDiffuseWeight = max( dotProduct, 0.0 );\\n\\n\\t\\t\\t#endif\\n\\n\\t\\t\\ttotalDiffuseLight += spotLightColor[ i ] * spotDiffuseWeight * attenuation * spotEffect;\\n\\n\\t\\t\\t// specular\\n\\n\\t\\t\\tvec3 spotHalfVector = normalize( lVector + viewPosition );\\n\\t\\t\\tfloat spotDotNormalHalf = max( dot( normal, spotHalfVector ), 0.0 );\\n\\t\\t\\tfloat spotSpecularWeight = specularStrength * max( pow( spotDotNormalHalf, shininess ), 0.0 );\\n\\n\\t\\t\\tfloat specularNormalization = ( shininess + 2.0 ) / 8.0;\\n\\n\\t\\t\\tvec3 schlick = specular + vec3( 1.0 - specular ) * pow( max( 1.0 - dot( lVector, spotHalfVector ), 0.0 ), 5.0 );\\n\\t\\t\\ttotalSpecularLight += schlick * spotLightColor[ i ] * spotSpecularWeight * spotDiffuseWeight * attenuation * specularNormalization * spotEffect;\\n\\n\\t\\t}\\n\\n\\t}\\n\\n#endif\\n\\n#if MAX_DIR_LIGHTS > 0\\n\\n\\tfor( int i = 0; i < MAX_DIR_LIGHTS; i ++ ) {\\n\\n\\t\\tvec3 dirVector = transformDirection( directionalLightDirection[ i ], viewMatrix );\\n\\n\\t\\t// diffuse\\n\\n\\t\\tfloat dotProduct = dot( normal, dirVector );\\n\\n\\t\\t#ifdef WRAP_AROUND\\n\\n\\t\\t\\tfloat dirDiffuseWeightFull = max( dotProduct, 0.0 );\\n\\t\\t\\tfloat dirDiffuseWeightHalf = max( 0.5 * dotProduct + 0.5, 0.0 );\\n\\n\\t\\t\\tvec3 dirDiffuseWeight = mix( vec3( dirDiffuseWeightFull ), vec3( dirDiffuseWeightHalf ), wrapRGB );\\n\\n\\t\\t#else\\n\\n\\t\\t\\tfloat dirDiffuseWeight = max( dotProduct, 0.0 );\\n\\n\\t\\t#endif\\n\\n\\t\\ttotalDiffuseLight += directionalLightColor[ i ] * dirDiffuseWeight;\\n\\n\\t\\t// specular\\n\\n\\t\\tvec3 dirHalfVector = normalize( dirVector + viewPosition );\\n\\t\\tfloat dirDotNormalHalf = max( dot( normal, dirHalfVector ), 0.0 );\\n\\t\\tfloat dirSpecularWeight = specularStrength * max( pow( dirDotNormalHalf, shininess ), 0.0 );\\n\\n\\t\\t/*\\n\\t\\t// fresnel term from skin shader\\n\\t\\tconst float F0 = 0.128;\\n\\n\\t\\tfloat base = 1.0 - dot( viewPosition, dirHalfVector );\\n\\t\\tfloat exponential = pow( base, 5.0 );\\n\\n\\t\\tfloat fresnel = exponential + F0 * ( 1.0 - exponential );\\n\\t\\t*/\\n\\n\\t\\t/*\\n\\t\\t// fresnel term from fresnel shader\\n\\t\\tconst float mFresnelBias = 0.08;\\n\\t\\tconst float mFresnelScale = 0.3;\\n\\t\\tconst float mFresnelPower = 5.0;\\n\\n\\t\\tfloat fresnel = mFresnelBias + mFresnelScale * pow( 1.0 + dot( normalize( -viewPosition ), normal ), mFresnelPower );\\n\\t\\t*/\\n\\n\\t\\tfloat specularNormalization = ( shininess + 2.0 ) / 8.0;\\n\\n\\t\\t// \\t\\tdirSpecular += specular * directionalLightColor[ i ] * dirSpecularWeight * dirDiffuseWeight * specularNormalization * fresnel;\\n\\n\\t\\tvec3 schlick = specular + vec3( 1.0 - specular ) * pow( max( 1.0 - dot( dirVector, dirHalfVector ), 0.0 ), 5.0 );\\n\\t\\ttotalSpecularLight += schlick * directionalLightColor[ i ] * dirSpecularWeight * dirDiffuseWeight * specularNormalization;\\n\\n\\n\\t}\\n\\n#endif\\n\\n#if MAX_HEMI_LIGHTS > 0\\n\\n\\tfor( int i = 0; i < MAX_HEMI_LIGHTS; i ++ ) {\\n\\n\\t\\tvec3 lVector = transformDirection( hemisphereLightDirection[ i ], viewMatrix );\\n\\n\\t\\t// diffuse\\n\\n\\t\\tfloat dotProduct = dot( normal, lVector );\\n\\t\\tfloat hemiDiffuseWeight = 0.5 * dotProduct + 0.5;\\n\\n\\t\\tvec3 hemiColor = mix( hemisphereLightGroundColor[ i ], hemisphereLightSkyColor[ i ], hemiDiffuseWeight );\\n\\n\\t\\ttotalDiffuseLight += hemiColor;\\n\\n\\t\\t// specular (sky light)\\n\\n\\t\\tvec3 hemiHalfVectorSky = normalize( lVector + viewPosition );\\n\\t\\tfloat hemiDotNormalHalfSky = 0.5 * dot( normal, hemiHalfVectorSky ) + 0.5;\\n\\t\\tfloat hemiSpecularWeightSky = specularStrength * max( pow( max( hemiDotNormalHalfSky, 0.0 ), shininess ), 0.0 );\\n\\n\\t\\t// specular (ground light)\\n\\n\\t\\tvec3 lVectorGround = -lVector;\\n\\n\\t\\tvec3 hemiHalfVectorGround = normalize( lVectorGround + viewPosition );\\n\\t\\tfloat hemiDotNormalHalfGround = 0.5 * dot( normal, hemiHalfVectorGround ) + 0.5;\\n\\t\\tfloat hemiSpecularWeightGround = specularStrength * max( pow( max( hemiDotNormalHalfGround, 0.0 ), shininess ), 0.0 );\\n\\n\\t\\tfloat dotProductGround = dot( normal, lVectorGround );\\n\\n\\t\\tfloat specularNormalization = ( shininess + 2.0 ) / 8.0;\\n\\n\\t\\tvec3 schlickSky = specular + vec3( 1.0 - specular ) * pow( max( 1.0 - dot( lVector, hemiHalfVectorSky ), 0.0 ), 5.0 );\\n\\t\\tvec3 schlickGround = specular + vec3( 1.0 - specular ) * pow( max( 1.0 - dot( lVectorGround, hemiHalfVectorGround ), 0.0 ), 5.0 );\\n\\t\\ttotalSpecularLight += hemiColor * specularNormalization * ( schlickSky * hemiSpecularWeightSky * max( dotProduct, 0.0 ) + schlickGround * hemiSpecularWeightGround * max( dotProductGround, 0.0 ) );\\n\\n\\t}\\n\\n#endif\\n\\n#ifdef METAL\\n\\n\\toutgoingLight += diffuseColor.rgb * ( totalDiffuseLight + ambientLightColor ) * specular + totalSpecularLight + emissive;\\n\\n#else\\n\\n\\toutgoingLight += diffuseColor.rgb * ( totalDiffuseLight + ambientLightColor ) + totalSpecularLight + emissive;\\n\\n#endif\\n\";"
                << std::endl ;
            out
                << "THREE.ShaderChunk.fog_pars_fragment=\"#ifdef USE_FOG\\n\\n\\tuniform vec3 fogColor;\\n\\n\\t#ifdef FOG_EXP2\\n\\n\\t\\tuniform float fogDensity;\\n\\n\\t#else\\n\\n\\t\\tuniform float fogNear;\\n\\t\\tuniform float fogFar;\\n\\t#endif\\n\\n#endif\";THREE.ShaderChunk.morphnormal_vertex=\"#ifdef USE_MORPHNORMALS\\n\\n\\tvec3 morphedNormal = vec3( 0.0 );\\n\\n\\tmorphedNormal += ( morphNormal0 - normal ) * morphTargetInfluences[ 0 ];\\n\\tmorphedNormal += ( morphNormal1 - normal ) * morphTargetInfluences[ 1 ];\\n\\tmorphedNormal += ( morphNormal2 - normal ) * morphTargetInfluences[ 2 ];\\n\\tmorphedNormal += ( morphNormal3 - normal ) * morphTargetInfluences[ 3 ];\\n\\n\\tmorphedNormal += normal;\\n\\n#endif\";"
                << std::endl ;
            out
                << "THREE.ShaderChunk.envmap_pars_fragment=\"#ifdef USE_ENVMAP\\n\\n\\tuniform float reflectivity;\\n\\t#ifdef ENVMAP_TYPE_CUBE\\n\\t\\tuniform samplerCube envMap;\\n\\t#else\\n\\t\\tuniform sampler2D envMap;\\n\\t#endif\\n\\tuniform float flipEnvMap;\\n\\n\\t#if defined( USE_BUMPMAP ) || defined( USE_NORMALMAP ) || defined( PHONG )\\n\\n\\t\\tuniform float refractionRatio;\\n\\n\\t#else\\n\\n\\t\\tvarying vec3 vReflect;\\n\\n\\t#endif\\n\\n#endif\\n\";THREE.ShaderChunk.logdepthbuf_fragment=\"#if defined(USE_LOGDEPTHBUF) && defined(USE_LOGDEPTHBUF_EXT)\\n\\n\\tgl_FragDepthEXT = log2(vFragDepth) * logDepthBufFC * 0.5;\\n\\n#endif\";"
                << std::endl ;
            out
                << "THREE.ShaderChunk.normalmap_pars_fragment=\"#ifdef USE_NORMALMAP\\n\\n\\tuniform sampler2D normalMap;\\n\\tuniform vec2 normalScale;\\n\\n\\t// Per-Pixel Tangent Space Normal Mapping\\n\\t// http://hacksoflife.blogspot.ch/2009/11/per-pixel-tangent-space-normal-mapping.html\\n\\n\\tvec3 perturbNormal2Arb( vec3 eye_pos, vec3 surf_norm ) {\\n\\n\\t\\tvec3 q0 = dFdx( eye_pos.xyz );\\n\\t\\tvec3 q1 = dFdy( eye_pos.xyz );\\n\\t\\tvec2 st0 = dFdx( vUv.st );\\n\\t\\tvec2 st1 = dFdy( vUv.st );\\n\\n\\t\\tvec3 S = normalize( q0 * st1.t - q1 * st0.t );\\n\\t\\tvec3 T = normalize( -q0 * st1.s + q1 * st0.s );\\n\\t\\tvec3 N = normalize( surf_norm );\\n\\n\\t\\tvec3 mapN = texture2D( normalMap, vUv ).xyz * 2.0 - 1.0;\\n\\t\\tmapN.xy = normalScale * mapN.xy;\\n\\t\\tmat3 tsn = mat3( S, T, N );\\n\\t\\treturn normalize( tsn * mapN );\\n\\n\\t}\\n\\n#endif\\n\";"
                << std::endl ;
            out
                << "THREE.ShaderChunk.lights_phong_pars_vertex=\"#if MAX_SPOT_LIGHTS > 0 || defined( USE_BUMPMAP ) || defined( USE_ENVMAP )\\n\\n\\tvarying vec3 vWorldPosition;\\n\\n#endif\\n\";THREE.ShaderChunk.lightmap_pars_fragment=\"#ifdef USE_LIGHTMAP\\n\\n\\tvarying vec2 vUv2;\\n\\tuniform sampler2D lightMap;\\n\\n#endif\";THREE.ShaderChunk.shadowmap_vertex=\"#ifdef USE_SHADOWMAP\\n\\n\\tfor( int i = 0; i < MAX_SHADOWS; i ++ ) {\\n\\n\\t\\tvShadowCoord[ i ] = shadowMatrix[ i ] * worldPosition;\\n\\n\\t}\\n\\n#endif\";"
                << std::endl ;
            out
                << "THREE.ShaderChunk.lights_phong_vertex=\"#if MAX_SPOT_LIGHTS > 0 || defined( USE_BUMPMAP ) || defined( USE_ENVMAP )\\n\\n\\tvWorldPosition = worldPosition.xyz;\\n\\n#endif\";THREE.ShaderChunk.map_fragment=\"#ifdef USE_MAP\\n\\n\\tvec4 texelColor = texture2D( map, vUv );\\n\\n\\ttexelColor.xyz = inputToLinear( texelColor.xyz );\\n\\n\\tdiffuseColor *= texelColor;\\n\\n#endif\";THREE.ShaderChunk.lightmap_vertex=\"#ifdef USE_LIGHTMAP\\n\\n\\tvUv2 = uv2;\\n\\n#endif\";THREE.ShaderChunk.map_particle_fragment=\"#ifdef USE_MAP\\n\\n\\tdiffuseColor *= texture2D( map, vec2( gl_PointCoord.x, 1.0 - gl_PointCoord.y ) * offsetRepeat.zw + offsetRepeat.xy );\\n\\n#endif\\n\";"
                << std::endl ;
            out
                << "THREE.ShaderChunk.color_pars_fragment=\"#ifdef USE_COLOR\\n\\n\\tvarying vec3 vColor;\\n\\n#endif\\n\";THREE.ShaderChunk.color_vertex=\"#ifdef USE_COLOR\\n\\n\\tvColor.xyz = inputToLinear( color.xyz );\\n\\n#endif\";THREE.ShaderChunk.skinning_vertex=\"#ifdef USE_SKINNING\\n\\n\\t#ifdef USE_MORPHTARGETS\\n\\n\\tvec4 skinVertex = bindMatrix * vec4( morphed, 1.0 );\\n\\n\\t#else\\n\\n\\tvec4 skinVertex = bindMatrix * vec4( position, 1.0 );\\n\\n\\t#endif\\n\\n\\tvec4 skinned = vec4( 0.0 );\\n\\tskinned += boneMatX * skinVertex * skinWeight.x;\\n\\tskinned += boneMatY * skinVertex * skinWeight.y;\\n\\tskinned += boneMatZ * skinVertex * skinWeight.z;\\n\\tskinned += boneMatW * skinVertex * skinWeight.w;\\n\\tskinned  = bindMatrixInverse * skinned;\\n\\n#endif\\n\";"
                << std::endl ;
            out
                << "THREE.ShaderChunk.envmap_pars_vertex=\"#if defined( USE_ENVMAP ) && ! defined( USE_BUMPMAP ) && ! defined( USE_NORMALMAP ) && ! defined( PHONG )\\n\\n\\tvarying vec3 vReflect;\\n\\n\\tuniform float refractionRatio;\\n\\n#endif\\n\";THREE.ShaderChunk.linear_to_gamma_fragment=\"\\n\\toutgoingLight = linearToOutput( outgoingLight );\\n\";THREE.ShaderChunk.color_pars_vertex=\"#ifdef USE_COLOR\\n\\n\\tvarying vec3 vColor;\\n\\n#endif\";THREE.ShaderChunk.lights_lambert_pars_vertex=\"uniform vec3 ambientLightColor;\\n\\n#if MAX_DIR_LIGHTS > 0\\n\\n\\tuniform vec3 directionalLightColor[ MAX_DIR_LIGHTS ];\\n\\tuniform vec3 directionalLightDirection[ MAX_DIR_LIGHTS ];\\n\\n#endif\\n\\n#if MAX_HEMI_LIGHTS > 0\\n\\n\\tuniform vec3 hemisphereLightSkyColor[ MAX_HEMI_LIGHTS ];\\n\\tuniform vec3 hemisphereLightGroundColor[ MAX_HEMI_LIGHTS ];\\n\\tuniform vec3 hemisphereLightDirection[ MAX_HEMI_LIGHTS ];\\n\\n#endif\\n\\n#if MAX_POINT_LIGHTS > 0\\n\\n\\tuniform vec3 pointLightColor[ MAX_POINT_LIGHTS ];\\n\\tuniform vec3 pointLightPosition[ MAX_POINT_LIGHTS ];\\n\\tuniform float pointLightDistance[ MAX_POINT_LIGHTS ];\\n\\tuniform float pointLightDecay[ MAX_POINT_LIGHTS ];\\n\\n#endif\\n\\n#if MAX_SPOT_LIGHTS > 0\\n\\n\\tuniform vec3 spotLightColor[ MAX_SPOT_LIGHTS ];\\n\\tuniform vec3 spotLightPosition[ MAX_SPOT_LIGHTS ];\\n\\tuniform vec3 spotLightDirection[ MAX_SPOT_LIGHTS ];\\n\\tuniform float spotLightDistance[ MAX_SPOT_LIGHTS ];\\n\\tuniform float spotLightAngleCos[ MAX_SPOT_LIGHTS ];\\n\\tuniform float spotLightExponent[ MAX_SPOT_LIGHTS ];\\n\\tuniform float spotLightDecay[ MAX_SPOT_LIGHTS ];\\n\\n#endif\\n\\n#ifdef WRAP_AROUND\\n\\n\\tuniform vec3 wrapRGB;\\n\\n#endif\\n\";"
                << std::endl ;
            out
                << "THREE.ShaderChunk.map_pars_vertex=\"#if defined( USE_MAP ) || defined( USE_BUMPMAP ) || defined( USE_NORMALMAP ) || defined( USE_SPECULARMAP ) || defined( USE_ALPHAMAP )\\n\\n\\tvarying vec2 vUv;\\n\\tuniform vec4 offsetRepeat;\\n\\n#endif\\n\";THREE.ShaderChunk.envmap_fragment=\"#ifdef USE_ENVMAP\\n\\n\\t#if defined( USE_BUMPMAP ) || defined( USE_NORMALMAP ) || defined( PHONG )\\n\\n\\t\\tvec3 cameraToVertex = normalize( vWorldPosition - cameraPosition );\\n\\n\\t\\t// Transforming Normal Vectors with the Inverse Transformation\\n\\t\\tvec3 worldNormal = inverseTransformDirection( normal, viewMatrix );\\n\\n\\t\\t#ifdef ENVMAP_MODE_REFLECTION\\n\\n\\t\\t\\tvec3 reflectVec = reflect( cameraToVertex, worldNormal );\\n\\n\\t\\t#else\\n\\n\\t\\t\\tvec3 reflectVec = refract( cameraToVertex, worldNormal, refractionRatio );\\n\\n\\t\\t#endif\\n\\n\\t#else\\n\\n\\t\\tvec3 reflectVec = vReflect;\\n\\n\\t#endif\\n\\n\\t#ifdef DOUBLE_SIDED\\n\\t\\tfloat flipNormal = ( -1.0 + 2.0 * float( gl_FrontFacing ) );\\n\\t#else\\n\\t\\tfloat flipNormal = 1.0;\\n\\t#endif\\n\\n\\t#ifdef ENVMAP_TYPE_CUBE\\n\\t\\tvec4 envColor = textureCube( envMap, flipNormal * vec3( flipEnvMap * reflectVec.x, reflectVec.yz ) );\\n\\n\\t#elif defined( ENVMAP_TYPE_EQUIREC )\\n\\t\\tvec2 sampleUV;\\n\\t\\tsampleUV.y = saturate( flipNormal * reflectVec.y * 0.5 + 0.5 );\\n\\t\\tsampleUV.x = atan( flipNormal * reflectVec.z, flipNormal * reflectVec.x ) * RECIPROCAL_PI2 + 0.5;\\n\\t\\tvec4 envColor = texture2D( envMap, sampleUV );\\n\\n\\t#elif defined( ENVMAP_TYPE_SPHERE )\\n\\t\\tvec3 reflectView = flipNormal * normalize((viewMatrix * vec4( reflectVec, 0.0 )).xyz + vec3(0.0,0.0,1.0));\\n\\t\\tvec4 envColor = texture2D( envMap, reflectView.xy * 0.5 + 0.5 );\\n\\t#endif\\n\\n\\tenvColor.xyz = inputToLinear( envColor.xyz );\\n\\n\\t#ifdef ENVMAP_BLENDING_MULTIPLY\\n\\n\\t\\toutgoingLight = mix( outgoingLight, outgoingLight * envColor.xyz, specularStrength * reflectivity );\\n\\n\\t#elif defined( ENVMAP_BLENDING_MIX )\\n\\n\\t\\toutgoingLight = mix( outgoingLight, envColor.xyz, specularStrength * reflectivity );\\n\\n\\t#elif defined( ENVMAP_BLENDING_ADD )\\n\\n\\t\\toutgoingLight += envColor.xyz * specularStrength * reflectivity;\\n\\n\\t#endif\\n\\n#endif\\n\";"
                << std::endl ;
            out
                << "THREE.ShaderChunk.specularmap_pars_fragment=\"#ifdef USE_SPECULARMAP\\n\\n\\tuniform sampler2D specularMap;\\n\\n#endif\";THREE.ShaderChunk.logdepthbuf_vertex=\"#ifdef USE_LOGDEPTHBUF\\n\\n\\tgl_Position.z = log2(max( EPSILON, gl_Position.w + 1.0 )) * logDepthBufFC;\\n\\n\\t#ifdef USE_LOGDEPTHBUF_EXT\\n\\n\\t\\tvFragDepth = 1.0 + gl_Position.w;\\n\\n#else\\n\\n\\t\\tgl_Position.z = (gl_Position.z - 1.0) * gl_Position.w;\\n\\n\\t#endif\\n\\n#endif\";THREE.ShaderChunk.morphtarget_pars_vertex=\"#ifdef USE_MORPHTARGETS\\n\\n\\t#ifndef USE_MORPHNORMALS\\n\\n\\tuniform float morphTargetInfluences[ 8 ];\\n\\n\\t#else\\n\\n\\tuniform float morphTargetInfluences[ 4 ];\\n\\n\\t#endif\\n\\n#endif\";"
                << std::endl ;
            out
                << "THREE.ShaderChunk.specularmap_fragment=\"float specularStrength;\\n\\n#ifdef USE_SPECULARMAP\\n\\n\\tvec4 texelSpecular = texture2D( specularMap, vUv );\\n\\tspecularStrength = texelSpecular.r;\\n\\n#else\\n\\n\\tspecularStrength = 1.0;\\n\\n#endif\";THREE.ShaderChunk.fog_fragment=\"#ifdef USE_FOG\\n\\n\\t#ifdef USE_LOGDEPTHBUF_EXT\\n\\n\\t\\tfloat depth = gl_FragDepthEXT / gl_FragCoord.w;\\n\\n\\t#else\\n\\n\\t\\tfloat depth = gl_FragCoord.z / gl_FragCoord.w;\\n\\n\\t#endif\\n\\n\\t#ifdef FOG_EXP2\\n\\n\\t\\tfloat fogFactor = exp2( - square( fogDensity ) * square( depth ) * LOG2 );\\n\\t\\tfogFactor = whiteCompliment( fogFactor );\\n\\n\\t#else\\n\\n\\t\\tfloat fogFactor = smoothstep( fogNear, fogFar, depth );\\n\\n\\t#endif\\n\\t\\n\\toutgoingLight = mix( outgoingLight, fogColor, fogFactor );\\n\\n#endif\";"
                << std::endl ;
            out
                << "THREE.ShaderChunk.bumpmap_pars_fragment=\"#ifdef USE_BUMPMAP\\n\\n\\tuniform sampler2D bumpMap;\\n\\tuniform float bumpScale;\\n\\n\\t// Derivative maps - bump mapping unparametrized surfaces by Morten Mikkelsen\\n\\t// http://mmikkelsen3d.blogspot.sk/2011/07/derivative-maps.html\\n\\n\\t// Evaluate the derivative of the height w.r.t. screen-space using forward differencing (listing 2)\\n\\n\\tvec2 dHdxy_fwd() {\\n\\n\\t\\tvec2 dSTdx = dFdx( vUv );\\n\\t\\tvec2 dSTdy = dFdy( vUv );\\n\\n\\t\\tfloat Hll = bumpScale * texture2D( bumpMap, vUv ).x;\\n\\t\\tfloat dBx = bumpScale * texture2D( bumpMap, vUv + dSTdx ).x - Hll;\\n\\t\\tfloat dBy = bumpScale * texture2D( bumpMap, vUv + dSTdy ).x - Hll;\\n\\n\\t\\treturn vec2( dBx, dBy );\\n\\n\\t}\\n\\n\\tvec3 perturbNormalArb( vec3 surf_pos, vec3 surf_norm, vec2 dHdxy ) {\\n\\n\\t\\tvec3 vSigmaX = dFdx( surf_pos );\\n\\t\\tvec3 vSigmaY = dFdy( surf_pos );\\n\\t\\tvec3 vN = surf_norm;\\t\\t// normalized\\n\\n\\t\\tvec3 R1 = cross( vSigmaY, vN );\\n\\t\\tvec3 R2 = cross( vN, vSigmaX );\\n\\n\\t\\tfloat fDet = dot( vSigmaX, R1 );\\n\\n\\t\\tvec3 vGrad = sign( fDet ) * ( dHdxy.x * R1 + dHdxy.y * R2 );\\n\\t\\treturn normalize( abs( fDet ) * surf_norm - vGrad );\\n\\n\\t}\\n\\n#endif\\n\";"
                << std::endl ;
            out
                << "THREE.ShaderChunk.defaultnormal_vertex=\"#ifdef USE_SKINNING\\n\\n\\tvec3 objectNormal = skinnedNormal.xyz;\\n\\n#elif defined( USE_MORPHNORMALS )\\n\\n\\tvec3 objectNormal = morphedNormal;\\n\\n#else\\n\\n\\tvec3 objectNormal = normal;\\n\\n#endif\\n\\n#ifdef FLIP_SIDED\\n\\n\\tobjectNormal = -objectNormal;\\n\\n#endif\\n\\nvec3 transformedNormal = normalMatrix * objectNormal;\\n\";THREE.ShaderChunk.lights_phong_pars_fragment=\"uniform vec3 ambientLightColor;\\n\\n#if MAX_DIR_LIGHTS > 0\\n\\n\\tuniform vec3 directionalLightColor[ MAX_DIR_LIGHTS ];\\n\\tuniform vec3 directionalLightDirection[ MAX_DIR_LIGHTS ];\\n\\n#endif\\n\\n#if MAX_HEMI_LIGHTS > 0\\n\\n\\tuniform vec3 hemisphereLightSkyColor[ MAX_HEMI_LIGHTS ];\\n\\tuniform vec3 hemisphereLightGroundColor[ MAX_HEMI_LIGHTS ];\\n\\tuniform vec3 hemisphereLightDirection[ MAX_HEMI_LIGHTS ];\\n\\n#endif\\n\\n#if MAX_POINT_LIGHTS > 0\\n\\n\\tuniform vec3 pointLightColor[ MAX_POINT_LIGHTS ];\\n\\n\\tuniform vec3 pointLightPosition[ MAX_POINT_LIGHTS ];\\n\\tuniform float pointLightDistance[ MAX_POINT_LIGHTS ];\\n\\tuniform float pointLightDecay[ MAX_POINT_LIGHTS ];\\n\\n#endif\\n\\n#if MAX_SPOT_LIGHTS > 0\\n\\n\\tuniform vec3 spotLightColor[ MAX_SPOT_LIGHTS ];\\n\\tuniform vec3 spotLightPosition[ MAX_SPOT_LIGHTS ];\\n\\tuniform vec3 spotLightDirection[ MAX_SPOT_LIGHTS ];\\n\\tuniform float spotLightAngleCos[ MAX_SPOT_LIGHTS ];\\n\\tuniform float spotLightExponent[ MAX_SPOT_LIGHTS ];\\n\\tuniform float spotLightDistance[ MAX_SPOT_LIGHTS ];\\n\\tuniform float spotLightDecay[ MAX_SPOT_LIGHTS ];\\n\\n#endif\\n\\n#if MAX_SPOT_LIGHTS > 0 || defined( USE_BUMPMAP ) || defined( USE_ENVMAP )\\n\\n\\tvarying vec3 vWorldPosition;\\n\\n#endif\\n\\n#ifdef WRAP_AROUND\\n\\n\\tuniform vec3 wrapRGB;\\n\\n#endif\\n\\nvarying vec3 vViewPosition;\\n\\n#ifndef FLAT_SHADED\\n\\n\\tvarying vec3 vNormal;\\n\\n#endif\\n\";"
                << std::endl ;
            out
                << "THREE.ShaderChunk.skinbase_vertex=\"#ifdef USE_SKINNING\\n\\n\\tmat4 boneMatX = getBoneMatrix( skinIndex.x );\\n\\tmat4 boneMatY = getBoneMatrix( skinIndex.y );\\n\\tmat4 boneMatZ = getBoneMatrix( skinIndex.z );\\n\\tmat4 boneMatW = getBoneMatrix( skinIndex.w );\\n\\n#endif\";THREE.ShaderChunk.map_vertex=\"#if defined( USE_MAP ) || defined( USE_BUMPMAP ) || defined( USE_NORMALMAP ) || defined( USE_SPECULARMAP ) || defined( USE_ALPHAMAP )\\n\\n\\tvUv = uv * offsetRepeat.zw + offsetRepeat.xy;\\n\\n#endif\";"
                << std::endl ;
            out
                << "THREE.ShaderChunk.lightmap_fragment=\"#ifdef USE_LIGHTMAP\\n\\n\\toutgoingLight *= diffuseColor.xyz * texture2D( lightMap, vUv2 ).xyz;\\n\\n#endif\";THREE.ShaderChunk.shadowmap_pars_vertex=\"#ifdef USE_SHADOWMAP\\n\\n\\tvarying vec4 vShadowCoord[ MAX_SHADOWS ];\\n\\tuniform mat4 shadowMatrix[ MAX_SHADOWS ];\\n\\n#endif\";THREE.ShaderChunk.color_fragment=\"#ifdef USE_COLOR\\n\\n\\tdiffuseColor.rgb *= vColor;\\n\\n#endif\";THREE.ShaderChunk.morphtarget_vertex=\"#ifdef USE_MORPHTARGETS\\n\\n\\tvec3 morphed = vec3( 0.0 );\\n\\tmorphed += ( morphTarget0 - position ) * morphTargetInfluences[ 0 ];\\n\\tmorphed += ( morphTarget1 - position ) * morphTargetInfluences[ 1 ];\\n\\tmorphed += ( morphTarget2 - position ) * morphTargetInfluences[ 2 ];\\n\\tmorphed += ( morphTarget3 - position ) * morphTargetInfluences[ 3 ];\\n\\n\\t#ifndef USE_MORPHNORMALS\\n\\n\\tmorphed += ( morphTarget4 - position ) * morphTargetInfluences[ 4 ];\\n\\tmorphed += ( morphTarget5 - position ) * morphTargetInfluences[ 5 ];\\n\\tmorphed += ( morphTarget6 - position ) * morphTargetInfluences[ 6 ];\\n\\tmorphed += ( morphTarget7 - position ) * morphTargetInfluences[ 7 ];\\n\\n\\t#endif\\n\\n\\tmorphed += position;\\n\\n#endif\";"
                << std::endl ;
            out
                << "THREE.ShaderChunk.envmap_vertex=\"#if defined( USE_ENVMAP ) && ! defined( USE_BUMPMAP ) && ! defined( USE_NORMALMAP ) && ! defined( PHONG )\\n\\n\\tvec3 worldNormal = transformDirection( objectNormal, modelMatrix );\\n\\n\\tvec3 cameraToVertex = normalize( worldPosition.xyz - cameraPosition );\\n\\n\\t#ifdef ENVMAP_MODE_REFLECTION\\n\\n\\t\\tvReflect = reflect( cameraToVertex, worldNormal );\\n\\n\\t#else\\n\\n\\t\\tvReflect = refract( cameraToVertex, worldNormal, refractionRatio );\\n\\n\\t#endif\\n\\n#endif\\n\";"
                << std::endl ;
            out
                << "THREE.ShaderChunk.shadowmap_fragment=\"#ifdef USE_SHADOWMAP\\n\\n\\t#ifdef SHADOWMAP_DEBUG\\n\\n\\t\\tvec3 frustumColors[3];\\n\\t\\tfrustumColors[0] = vec3( 1.0, 0.5, 0.0 );\\n\\t\\tfrustumColors[1] = vec3( 0.0, 1.0, 0.8 );\\n\\t\\tfrustumColors[2] = vec3( 0.0, 0.5, 1.0 );\\n\\n\\t#endif\\n\\n\\t#ifdef SHADOWMAP_CASCADE\\n\\n\\t\\tint inFrustumCount = 0;\\n\\n\\t#endif\\n\\n\\tfloat fDepth;\\n\\tvec3 shadowColor = vec3( 1.0 );\\n\\n\\tfor( int i = 0; i < MAX_SHADOWS; i ++ ) {\\n\\n\\t\\tvec3 shadowCoord = vShadowCoord[ i ].xyz / vShadowCoord[ i ].w;\\n\\n\\t\\t\\t\\t// if ( something && something ) breaks ATI OpenGL shader compiler\\n\\t\\t\\t\\t// if ( all( something, something ) ) using this instead\\n\\n\\t\\tbvec4 inFrustumVec = bvec4 ( shadowCoord.x >= 0.0, shadowCoord.x <= 1.0, shadowCoord.y >= 0.0, shadowCoord.y <= 1.0 );\\n\\t\\tbool inFrustum = all( inFrustumVec );\\n\\n\\t\\t\\t\\t// don't shadow pixels outside of light frustum\\n\\t\\t\\t\\t// use just first frustum (for cascades)\\n\\t\\t\\t\\t// don't shadow pixels behind far plane of light frustum\\n\\n\\t\\t#ifdef SHADOWMAP_CASCADE\\n\\n\\t\\t\\tinFrustumCount += int( inFrustum );\\n\\t\\t\\tbvec3 frustumTestVec = bvec3( inFrustum, inFrustumCount == 1, shadowCoord.z <= 1.0 );\\n\\n\\t\\t#else\\n\\n\\t\\t\\tbvec2 frustumTestVec = bvec2( inFrustum, shadowCoord.z <= 1.0 );\\n\\n\\t\\t#endif\\n\\n\\t\\tbool frustumTest = all( frustumTestVec );\\n\\n\\t\\tif ( frustumTest ) {\\n\\n\\t\\t\\tshadowCoord.z += shadowBias[ i ];\\n\\n\\t\\t\\t#if defined( SHADOWMAP_TYPE_PCF )\\n\\n\\t\\t\\t\\t\\t\\t// Percentage-close filtering\\n\\t\\t\\t\\t\\t\\t// (9 pixel kernel)\\n\\t\\t\\t\\t\\t\\t// http://fabiensanglard.net/shadowmappingPCF/\\n\\n\\t\\t\\t\\tfloat shadow = 0.0;\\n\\n\\t\\t/*\\n\\t\\t\\t\\t\\t\\t// nested loops breaks shader compiler / validator on some ATI cards when using OpenGL\\n\\t\\t\\t\\t\\t\\t// must enroll loop manually\\n\\n\\t\\t\\t\\tfor ( float y = -1.25; y <= 1.25; y += 1.25 )\\n\\t\\t\\t\\t\\tfor ( float x = -1.25; x <= 1.25; x += 1.25 ) {\\n\\n\\t\\t\\t\\t\\t\\tvec4 rgbaDepth = texture2D( shadowMap[ i ], vec2( x * xPixelOffset, y * yPixelOffset ) + shadowCoord.xy );\\n\\n\\t\\t\\t\\t\\t\\t\\t\\t// doesn't seem to produce any noticeable visual difference compared to simple texture2D lookup\\n\\t\\t\\t\\t\\t\\t\\t\\t//vec4 rgbaDepth = texture2DProj( shadowMap[ i ], vec4( vShadowCoord[ i ].w * ( vec2( x * xPixelOffset, y * yPixelOffset ) + shadowCoord.xy ), 0.05, vShadowCoord[ i ].w ) );\\n\\n\\t\\t\\t\\t\\t\\tfloat fDepth = unpackDepth( rgbaDepth );\\n\\n\\t\\t\\t\\t\\t\\tif ( fDepth < shadowCoord.z )\\n\\t\\t\\t\\t\\t\\t\\tshadow += 1.0;\\n\\n\\t\\t\\t\\t}\\n\\n\\t\\t\\t\\tshadow /= 9.0;\\n\\n\\t\\t*/\\n\\n\\t\\t\\t\\tconst float shadowDelta = 1.0 / 9.0;\\n\\n\\t\\t\\t\\tfloat xPixelOffset = 1.0 / shadowMapSize[ i ].x;\\n\\t\\t\\t\\tfloat yPixelOffset = 1.0 / shadowMapSize[ i ].y;\\n\\n\\t\\t\\t\\tfloat dx0 = -1.25 * xPixelOffset;\\n\\t\\t\\t\\tfloat dy0 = -1.25 * yPixelOffset;\\n\\t\\t\\t\\tfloat dx1 = 1.25 * xPixelOffset;\\n\\t\\t\\t\\tfloat dy1 = 1.25 * yPixelOffset;\\n\\n\\t\\t\\t\\tfDepth = unpackDepth( texture2D( shadowMap[ i ], shadowCoord.xy + vec2( dx0, dy0 ) ) );\\n\\t\\t\\t\\tif ( fDepth < shadowCoord.z ) shadow += shadowDelta;\\n\\n\\t\\t\\t\\tfDepth = unpackDepth( texture2D( shadowMap[ i ], shadowCoord.xy + vec2( 0.0, dy0 ) ) );\\n\\t\\t\\t\\tif ( fDepth < shadowCoord.z ) shadow += shadowDelta;\\n\\n\\t\\t\\t\\tfDepth = unpackDepth( texture2D( shadowMap[ i ], shadowCoord.xy + vec2( dx1, dy0 ) ) );\\n\\t\\t\\t\\tif ( fDepth < shadowCoord.z ) shadow += shadowDelta;\\n\\n\\t\\t\\t\\tfDepth = unpackDepth( texture2D( shadowMap[ i ], shadowCoord.xy + vec2( dx0, 0.0 ) ) );\\n\\t\\t\\t\\tif ( fDepth < shadowCoord.z ) shadow += shadowDelta;\\n\\n\\t\\t\\t\\tfDepth = unpackDepth( texture2D( shadowMap[ i ], shadowCoord.xy ) );\\n\\t\\t\\t\\tif ( fDepth < shadowCoord.z ) shadow += shadowDelta;\\n\\n\\t\\t\\t\\tfDepth = unpackDepth( texture2D( shadowMap[ i ], shadowCoord.xy + vec2( dx1, 0.0 ) ) );\\n\\t\\t\\t\\tif ( fDepth < shadowCoord.z ) shadow += shadowDelta;\\n\\n\\t\\t\\t\\tfDepth = unpackDepth( texture2D( shadowMap[ i ], shadowCoord.xy + vec2( dx0, dy1 ) ) );\\n\\t\\t\\t\\tif ( fDepth < shadowCoord.z ) shadow += shadowDelta;\\n\\n\\t\\t\\t\\tfDepth = unpackDepth( texture2D( shadowMap[ i ], shadowCoord.xy + vec2( 0.0, dy1 ) ) );\\n\\t\\t\\t\\tif ( fDepth < shadowCoord.z ) shadow += shadowDelta;\\n\\n\\t\\t\\t\\tfDepth = unpackDepth( texture2D( shadowMap[ i ], shadowCoord.xy + vec2( dx1, dy1 ) ) );\\n\\t\\t\\t\\tif ( fDepth < shadowCoord.z ) shadow += shadowDelta;\\n\\n\\t\\t\\t\\tshadowColor = shadowColor * vec3( ( 1.0 - shadowDarkness[ i ] * shadow ) );\\n\\n\\t\\t\\t#elif defined( SHADOWMAP_TYPE_PCF_SOFT )\\n\\n\\t\\t\\t\\t\\t\\t// Percentage-close filtering\\n\\t\\t\\t\\t\\t\\t// (9 pixel kernel)\\n\\t\\t\\t\\t\\t\\t// http://fabiensanglard.net/shadowmappingPCF/\\n\\n\\t\\t\\t\\tfloat shadow = 0.0;\\n\\n\\t\\t\\t\\tfloat xPixelOffset = 1.0 / shadowMapSize[ i ].x;\\n\\t\\t\\t\\tfloat yPixelOffset = 1.0 / shadowMapSize[ i ].y;\\n\\n\\t\\t\\t\\tfloat dx0 = -1.0 * xPixelOffset;\\n\\t\\t\\t\\tfloat dy0 = -1.0 * yPixelOffset;\\n\\t\\t\\t\\tfloat dx1 = 1.0 * xPixelOffset;\\n\\t\\t\\t\\tfloat dy1 = 1.0 * yPixelOffset;\\n\\n\\t\\t\\t\\tmat3 shadowKernel;\\n\\t\\t\\t\\tmat3 depthKernel;\\n\\n\\t\\t\\t\\tdepthKernel[0][0] = unpackDepth( texture2D( shadowMap[ i ], shadowCoord.xy + vec2( dx0, dy0 ) ) );\\n\\t\\t\\t\\tdepthKernel[0][1] = unpackDepth( texture2D( shadowMap[ i ], shadowCoord.xy + vec2( dx0, 0.0 ) ) );\\n\\t\\t\\t\\tdepthKernel[0][2] = unpackDepth( texture2D( shadowMap[ i ], shadowCoord.xy + vec2( dx0, dy1 ) ) );\\n\\t\\t\\t\\tdepthKernel[1][0] = unpackDepth( texture2D( shadowMap[ i ], shadowCoord.xy + vec2( 0.0, dy0 ) ) );\\n\\t\\t\\t\\tdepthKernel[1][1] = unpackDepth( texture2D( shadowMap[ i ], shadowCoord.xy ) );\\n\\t\\t\\t\\tdepthKernel[1][2] = unpackDepth( texture2D( shadowMap[ i ], shadowCoord.xy + vec2( 0.0, dy1 ) ) );\\n\\t\\t\\t\\tdepthKernel[2][0] = unpackDepth( texture2D( shadowMap[ i ], shadowCoord.xy + vec2( dx1, dy0 ) ) );\\n\\t\\t\\t\\tdepthKernel[2][1] = unpackDepth( texture2D( shadowMap[ i ], shadowCoord.xy + vec2( dx1, 0.0 ) ) );\\n\\t\\t\\t\\tdepthKernel[2][2] = unpackDepth( texture2D( shadowMap[ i ], shadowCoord.xy + vec2( dx1, dy1 ) ) );\\n\\n\\t\\t\\t\\tvec3 shadowZ = vec3( shadowCoord.z );\\n\\t\\t\\t\\tshadowKernel[0] = vec3(lessThan(depthKernel[0], shadowZ ));\\n\\t\\t\\t\\tshadowKernel[0] *= vec3(0.25);\\n\\n\\t\\t\\t\\tshadowKernel[1] = vec3(lessThan(depthKernel[1], shadowZ ));\\n\\t\\t\\t\\tshadowKernel[1] *= vec3(0.25);\\n\\n\\t\\t\\t\\tshadowKernel[2] = vec3(lessThan(depthKernel[2], shadowZ ));\\n\\t\\t\\t\\tshadowKernel[2] *= vec3(0.25);\\n\\n\\t\\t\\t\\tvec2 fractionalCoord = 1.0 - fract( shadowCoord.xy * shadowMapSize[i].xy );\\n\\n\\t\\t\\t\\tshadowKernel[0] = mix( shadowKernel[1], shadowKernel[0], fractionalCoord.x );\\n\\t\\t\\t\\tshadowKernel[1] = mix( shadowKernel[2], shadowKernel[1], fractionalCoord.x );\\n\\n\\t\\t\\t\\tvec4 shadowValues;\\n\\t\\t\\t\\tshadowValues.x = mix( shadowKernel[0][1], shadowKernel[0][0], fractionalCoord.y );\\n\\t\\t\\t\\tshadowValues.y = mix( shadowKernel[0][2], shadowKernel[0][1], fractionalCoord.y );\\n\\t\\t\\t\\tshadowValues.z = mix( shadowKernel[1][1], shadowKernel[1][0], fractionalCoord.y );\\n\\t\\t\\t\\tshadowValues.w = mix( shadowKernel[1][2], shadowKernel[1][1], fractionalCoord.y );\\n\\n\\t\\t\\t\\tshadow = dot( shadowValues, vec4( 1.0 ) );\\n\\n\\t\\t\\t\\tshadowColor = shadowColor * vec3( ( 1.0 - shadowDarkness[ i ] * shadow ) );\\n\\n\\t\\t\\t#else\\n\\n\\t\\t\\t\\tvec4 rgbaDepth = texture2D( shadowMap[ i ], shadowCoord.xy );\\n\\t\\t\\t\\tfloat fDepth = unpackDepth( rgbaDepth );\\n\\n\\t\\t\\t\\tif ( fDepth < shadowCoord.z )\\n\\n\\t\\t// spot with multiple shadows is darker\\n\\n\\t\\t\\t\\t\\tshadowColor = shadowColor * vec3( 1.0 - shadowDarkness[ i ] );\\n\\n\\t\\t// spot with multiple shadows has the same color as single shadow spot\\n\\n\\t\\t// \\t\\t\\t\\t\\tshadowColor = min( shadowColor, vec3( shadowDarkness[ i ] ) );\\n\\n\\t\\t\\t#endif\\n\\n\\t\\t}\\n\\n\\n\\t\\t#ifdef SHADOWMAP_DEBUG\\n\\n\\t\\t\\t#ifdef SHADOWMAP_CASCADE\\n\\n\\t\\t\\t\\tif ( inFrustum && inFrustumCount == 1 ) outgoingLight *= frustumColors[ i ];\\n\\n\\t\\t\\t#else\\n\\n\\t\\t\\t\\tif ( inFrustum ) outgoingLight *= frustumColors[ i ];\\n\\n\\t\\t\\t#endif\\n\\n\\t\\t#endif\\n\\n\\t}\\n\\n\\t// NOTE: I am unsure if this is correct in linear space.  -bhouston, Dec 29, 2014\\n\\tshadowColor = inputToLinear( shadowColor );\\n\\n\\toutgoingLight = outgoingLight * shadowColor;\\n\\n#endif\\n\";"
                << std::endl ;
            out
                << "THREE.ShaderChunk.worldpos_vertex=\"#if defined( USE_ENVMAP ) || defined( PHONG ) || defined( LAMBERT ) || defined ( USE_SHADOWMAP )\\n\\n\\t#ifdef USE_SKINNING\\n\\n\\t\\tvec4 worldPosition = modelMatrix * skinned;\\n\\n\\t#elif defined( USE_MORPHTARGETS )\\n\\n\\t\\tvec4 worldPosition = modelMatrix * vec4( morphed, 1.0 );\\n\\n\\t#else\\n\\n\\t\\tvec4 worldPosition = modelMatrix * vec4( position, 1.0 );\\n\\n\\t#endif\\n\\n#endif\\n\";THREE.ShaderChunk.shadowmap_pars_fragment=\"#ifdef USE_SHADOWMAP\\n\\n\\tuniform sampler2D shadowMap[ MAX_SHADOWS ];\\n\\tuniform vec2 shadowMapSize[ MAX_SHADOWS ];\\n\\n\\tuniform float shadowDarkness[ MAX_SHADOWS ];\\n\\tuniform float shadowBias[ MAX_SHADOWS ];\\n\\n\\tvarying vec4 vShadowCoord[ MAX_SHADOWS ];\\n\\n\\tfloat unpackDepth( const in vec4 rgba_depth ) {\\n\\n\\t\\tconst vec4 bit_shift = vec4( 1.0 / ( 256.0 * 256.0 * 256.0 ), 1.0 / ( 256.0 * 256.0 ), 1.0 / 256.0, 1.0 );\\n\\t\\tfloat depth = dot( rgba_depth, bit_shift );\\n\\t\\treturn depth;\\n\\n\\t}\\n\\n#endif\";"
                << std::endl ;
            out
                << "THREE.ShaderChunk.skinning_pars_vertex=\"#ifdef USE_SKINNING\\n\\n\\tuniform mat4 bindMatrix;\\n\\tuniform mat4 bindMatrixInverse;\\n\\n\\t#ifdef BONE_TEXTURE\\n\\n\\t\\tuniform sampler2D boneTexture;\\n\\t\\tuniform int boneTextureWidth;\\n\\t\\tuniform int boneTextureHeight;\\n\\n\\t\\tmat4 getBoneMatrix( const in float i ) {\\n\\n\\t\\t\\tfloat j = i * 4.0;\\n\\t\\t\\tfloat x = mod( j, float( boneTextureWidth ) );\\n\\t\\t\\tfloat y = floor( j / float( boneTextureWidth ) );\\n\\n\\t\\t\\tfloat dx = 1.0 / float( boneTextureWidth );\\n\\t\\t\\tfloat dy = 1.0 / float( boneTextureHeight );\\n\\n\\t\\t\\ty = dy * ( y + 0.5 );\\n\\n\\t\\t\\tvec4 v1 = texture2D( boneTexture, vec2( dx * ( x + 0.5 ), y ) );\\n\\t\\t\\tvec4 v2 = texture2D( boneTexture, vec2( dx * ( x + 1.5 ), y ) );\\n\\t\\t\\tvec4 v3 = texture2D( boneTexture, vec2( dx * ( x + 2.5 ), y ) );\\n\\t\\t\\tvec4 v4 = texture2D( boneTexture, vec2( dx * ( x + 3.5 ), y ) );\\n\\n\\t\\t\\tmat4 bone = mat4( v1, v2, v3, v4 );\\n\\n\\t\\t\\treturn bone;\\n\\n\\t\\t}\\n\\n\\t#else\\n\\n\\t\\tuniform mat4 boneGlobalMatrices[ MAX_BONES ];\\n\\n\\t\\tmat4 getBoneMatrix( const in float i ) {\\n\\n\\t\\t\\tmat4 bone = boneGlobalMatrices[ int(i) ];\\n\\t\\t\\treturn bone;\\n\\n\\t\\t}\\n\\n\\t#endif\\n\\n#endif\\n\";"
                << std::endl ;
            out
                << "THREE.ShaderChunk.logdepthbuf_pars_fragment=\"#ifdef USE_LOGDEPTHBUF\\n\\n\\tuniform float logDepthBufFC;\\n\\n\\t#ifdef USE_LOGDEPTHBUF_EXT\\n\\n\\t\\t#extension GL_EXT_frag_depth : enable\\n\\t\\tvarying float vFragDepth;\\n\\n\\t#endif\\n\\n#endif\";THREE.ShaderChunk.alphamap_fragment=\"#ifdef USE_ALPHAMAP\\n\\n\\tdiffuseColor.a *= texture2D( alphaMap, vUv ).g;\\n\\n#endif\\n\";THREE.ShaderChunk.alphamap_pars_fragment=\"#ifdef USE_ALPHAMAP\\n\\n\\tuniform sampler2D alphaMap;\\n\\n#endif\\n\";"
                << std::endl ;
            out
                << "THREE.UniformsUtils={merge:function(a){for(var b={},c=0;c<a.length;c++){var d=this.clone(a[c]),e;for(e in d)b[e]=d[e]}return b},clone:function(a){var b={},c;for(c in a){b[c]={};for(var d in a[c]){var e=a[c][d];b[c][d]=e instanceof THREE.Color||e instanceof THREE.Vector2||e instanceof THREE.Vector3||e instanceof THREE.Vector4||e instanceof THREE.Matrix4||e instanceof THREE.Texture?e.clone():e instanceof Array?e.slice():e}}return b}};"
                << std::endl ;
            out
                << "THREE.UniformsLib={common:{diffuse:{type:\"c\",value:new THREE.Color(15658734)},opacity:{type:\"f\",value:1},map:{type:\"t\",value:null},offsetRepeat:{type:\"v4\",value:new THREE.Vector4(0,0,1,1)},lightMap:{type:\"t\",value:null},specularMap:{type:\"t\",value:null},alphaMap:{type:\"t\",value:null},envMap:{type:\"t\",value:null},flipEnvMap:{type:\"f\",value:-1},reflectivity:{type:\"f\",value:1},refractionRatio:{type:\"f\",value:.98},morphTargetInfluences:{type:\"f\",value:0}},bump:{bumpMap:{type:\"t\",value:null},bumpScale:{type:\"f\","
                << std::endl ;
            out
                << "value:1}},normalmap:{normalMap:{type:\"t\",value:null},normalScale:{type:\"v2\",value:new THREE.Vector2(1,1)}},fog:{fogDensity:{type:\"f\",value:2.5E-4},fogNear:{type:\"f\",value:1},fogFar:{type:\"f\",value:2E3},fogColor:{type:\"c\",value:new THREE.Color(16777215)}},lights:{ambientLightColor:{type:\"fv\",value:[]},directionalLightDirection:{type:\"fv\",value:[]},directionalLightColor:{type:\"fv\",value:[]},hemisphereLightDirection:{type:\"fv\",value:[]},hemisphereLightSkyColor:{type:\"fv\",value:[]},hemisphereLightGroundColor:{type:\"fv\","
                << std::endl ;
            out
                << "value:[]},pointLightColor:{type:\"fv\",value:[]},pointLightPosition:{type:\"fv\",value:[]},pointLightDistance:{type:\"fv1\",value:[]},pointLightDecay:{type:\"fv1\",value:[]},spotLightColor:{type:\"fv\",value:[]},spotLightPosition:{type:\"fv\",value:[]},spotLightDirection:{type:\"fv\",value:[]},spotLightDistance:{type:\"fv1\",value:[]},spotLightAngleCos:{type:\"fv1\",value:[]},spotLightExponent:{type:\"fv1\",value:[]},spotLightDecay:{type:\"fv1\",value:[]}},particle:{psColor:{type:\"c\",value:new THREE.Color(15658734)},opacity:{type:\"f\","
                << std::endl ;
            out
                << "value:1},size:{type:\"f\",value:1},scale:{type:\"f\",value:1},map:{type:\"t\",value:null},offsetRepeat:{type:\"v4\",value:new THREE.Vector4(0,0,1,1)},fogDensity:{type:\"f\",value:2.5E-4},fogNear:{type:\"f\",value:1},fogFar:{type:\"f\",value:2E3},fogColor:{type:\"c\",value:new THREE.Color(16777215)}},shadowmap:{shadowMap:{type:\"tv\",value:[]},shadowMapSize:{type:\"v2v\",value:[]},shadowBias:{type:\"fv1\",value:[]},shadowDarkness:{type:\"fv1\",value:[]},shadowMatrix:{type:\"m4v\",value:[]}}};"
                << std::endl ;
            out
                << "THREE.ShaderLib={basic:{uniforms:THREE.UniformsUtils.merge([THREE.UniformsLib.common,THREE.UniformsLib.fog,THREE.UniformsLib.shadowmap]),vertexShader:[THREE.ShaderChunk.common,THREE.ShaderChunk.map_pars_vertex,THREE.ShaderChunk.lightmap_pars_vertex,THREE.ShaderChunk.envmap_pars_vertex,THREE.ShaderChunk.color_pars_vertex,THREE.ShaderChunk.morphtarget_pars_vertex,THREE.ShaderChunk.skinning_pars_vertex,THREE.ShaderChunk.shadowmap_pars_vertex,THREE.ShaderChunk.logdepthbuf_pars_vertex,\"void main() {\","
                << std::endl ;
            out
                << "THREE.ShaderChunk.map_vertex,THREE.ShaderChunk.lightmap_vertex,THREE.ShaderChunk.color_vertex,THREE.ShaderChunk.skinbase_vertex,\"\\t#ifdef USE_ENVMAP\",THREE.ShaderChunk.morphnormal_vertex,THREE.ShaderChunk.skinnormal_vertex,THREE.ShaderChunk.defaultnormal_vertex,\"\\t#endif\",THREE.ShaderChunk.morphtarget_vertex,THREE.ShaderChunk.skinning_vertex,THREE.ShaderChunk.default_vertex,THREE.ShaderChunk.logdepthbuf_vertex,THREE.ShaderChunk.worldpos_vertex,THREE.ShaderChunk.envmap_vertex,THREE.ShaderChunk.shadowmap_vertex,"
                << std::endl ;
            out
                << "\"}\"].join(\"\\n\"),fragmentShader:[\"uniform vec3 diffuse;\\nuniform float opacity;\",THREE.ShaderChunk.common,THREE.ShaderChunk.color_pars_fragment,THREE.ShaderChunk.map_pars_fragment,THREE.ShaderChunk.alphamap_pars_fragment,THREE.ShaderChunk.lightmap_pars_fragment,THREE.ShaderChunk.envmap_pars_fragment,THREE.ShaderChunk.fog_pars_fragment,THREE.ShaderChunk.shadowmap_pars_fragment,THREE.ShaderChunk.specularmap_pars_fragment,THREE.ShaderChunk.logdepthbuf_pars_fragment,\"void main() {\\n\\tvec3 outgoingLight = vec3( 0.0 );\\n\\tvec4 diffuseColor = vec4( diffuse, opacity );\","
                << std::endl ;
            out
                << "THREE.ShaderChunk.logdepthbuf_fragment,THREE.ShaderChunk.map_fragment,THREE.ShaderChunk.color_fragment,THREE.ShaderChunk.alphamap_fragment,THREE.ShaderChunk.alphatest_fragment,THREE.ShaderChunk.specularmap_fragment,\"\\toutgoingLight = diffuseColor.rgb;\",THREE.ShaderChunk.lightmap_fragment,THREE.ShaderChunk.envmap_fragment,THREE.ShaderChunk.shadowmap_fragment,THREE.ShaderChunk.linear_to_gamma_fragment,THREE.ShaderChunk.fog_fragment,\"\\tgl_FragColor = vec4( outgoingLight, diffuseColor.a );\\n}\"].join(\"\\n\")},"
                << std::endl ;
            out
                << "lambert:{uniforms:THREE.UniformsUtils.merge([THREE.UniformsLib.common,THREE.UniformsLib.fog,THREE.UniformsLib.lights,THREE.UniformsLib.shadowmap,{emissive:{type:\"c\",value:new THREE.Color(0)},wrapRGB:{type:\"v3\",value:new THREE.Vector3(1,1,1)}}]),vertexShader:[\"#define LAMBERT\\nvarying vec3 vLightFront;\\n#ifdef DOUBLE_SIDED\\n\\tvarying vec3 vLightBack;\\n#endif\",THREE.ShaderChunk.common,THREE.ShaderChunk.map_pars_vertex,THREE.ShaderChunk.lightmap_pars_vertex,THREE.ShaderChunk.envmap_pars_vertex,THREE.ShaderChunk.lights_lambert_pars_vertex,"
                << std::endl ;
            out
                << "THREE.ShaderChunk.color_pars_vertex,THREE.ShaderChunk.morphtarget_pars_vertex,THREE.ShaderChunk.skinning_pars_vertex,THREE.ShaderChunk.shadowmap_pars_vertex,THREE.ShaderChunk.logdepthbuf_pars_vertex,\"void main() {\",THREE.ShaderChunk.map_vertex,THREE.ShaderChunk.lightmap_vertex,THREE.ShaderChunk.color_vertex,THREE.ShaderChunk.morphnormal_vertex,THREE.ShaderChunk.skinbase_vertex,THREE.ShaderChunk.skinnormal_vertex,THREE.ShaderChunk.defaultnormal_vertex,THREE.ShaderChunk.morphtarget_vertex,THREE.ShaderChunk.skinning_vertex,"
                << std::endl ;
            out
                << "THREE.ShaderChunk.default_vertex,THREE.ShaderChunk.logdepthbuf_vertex,THREE.ShaderChunk.worldpos_vertex,THREE.ShaderChunk.envmap_vertex,THREE.ShaderChunk.lights_lambert_vertex,THREE.ShaderChunk.shadowmap_vertex,\"}\"].join(\"\\n\"),fragmentShader:[\"uniform vec3 diffuse;\\nuniform vec3 emissive;\\nuniform float opacity;\\nvarying vec3 vLightFront;\\n#ifdef DOUBLE_SIDED\\n\\tvarying vec3 vLightBack;\\n#endif\",THREE.ShaderChunk.common,THREE.ShaderChunk.color_pars_fragment,THREE.ShaderChunk.map_pars_fragment,THREE.ShaderChunk.alphamap_pars_fragment,"
                << std::endl ;
            out
                << "THREE.ShaderChunk.lightmap_pars_fragment,THREE.ShaderChunk.envmap_pars_fragment,THREE.ShaderChunk.fog_pars_fragment,THREE.ShaderChunk.shadowmap_pars_fragment,THREE.ShaderChunk.specularmap_pars_fragment,THREE.ShaderChunk.logdepthbuf_pars_fragment,\"void main() {\\n\\tvec3 outgoingLight = vec3( 0.0 );\\n\\tvec4 diffuseColor = vec4( diffuse, opacity );\",THREE.ShaderChunk.logdepthbuf_fragment,THREE.ShaderChunk.map_fragment,THREE.ShaderChunk.color_fragment,THREE.ShaderChunk.alphamap_fragment,THREE.ShaderChunk.alphatest_fragment,"
                << std::endl ;
            out
                << "THREE.ShaderChunk.specularmap_fragment,\"\\t#ifdef DOUBLE_SIDED\\n\\t\\tif ( gl_FrontFacing )\\n\\t\\t\\toutgoingLight += diffuseColor.rgb * vLightFront + emissive;\\n\\t\\telse\\n\\t\\t\\toutgoingLight += diffuseColor.rgb * vLightBack + emissive;\\n\\t#else\\n\\t\\toutgoingLight += diffuseColor.rgb * vLightFront + emissive;\\n\\t#endif\",THREE.ShaderChunk.lightmap_fragment,THREE.ShaderChunk.envmap_fragment,THREE.ShaderChunk.shadowmap_fragment,THREE.ShaderChunk.linear_to_gamma_fragment,THREE.ShaderChunk.fog_fragment,\"\\tgl_FragColor = vec4( outgoingLight, diffuseColor.a );\\n}\"].join(\"\\n\")},"
                << std::endl ;
            out
                << "phong:{uniforms:THREE.UniformsUtils.merge([THREE.UniformsLib.common,THREE.UniformsLib.bump,THREE.UniformsLib.normalmap,THREE.UniformsLib.fog,THREE.UniformsLib.lights,THREE.UniformsLib.shadowmap,{emissive:{type:\"c\",value:new THREE.Color(0)},specular:{type:\"c\",value:new THREE.Color(1118481)},shininess:{type:\"f\",value:30},wrapRGB:{type:\"v3\",value:new THREE.Vector3(1,1,1)}}]),vertexShader:[\"#define PHONG\\nvarying vec3 vViewPosition;\\n#ifndef FLAT_SHADED\\n\\tvarying vec3 vNormal;\\n#endif\",THREE.ShaderChunk.common,"
                << std::endl ;
            out
                << "THREE.ShaderChunk.map_pars_vertex,THREE.ShaderChunk.lightmap_pars_vertex,THREE.ShaderChunk.envmap_pars_vertex,THREE.ShaderChunk.lights_phong_pars_vertex,THREE.ShaderChunk.color_pars_vertex,THREE.ShaderChunk.morphtarget_pars_vertex,THREE.ShaderChunk.skinning_pars_vertex,THREE.ShaderChunk.shadowmap_pars_vertex,THREE.ShaderChunk.logdepthbuf_pars_vertex,\"void main() {\",THREE.ShaderChunk.map_vertex,THREE.ShaderChunk.lightmap_vertex,THREE.ShaderChunk.color_vertex,THREE.ShaderChunk.morphnormal_vertex,THREE.ShaderChunk.skinbase_vertex,"
                << std::endl ;
            out
                << "THREE.ShaderChunk.skinnormal_vertex,THREE.ShaderChunk.defaultnormal_vertex,\"#ifndef FLAT_SHADED\\n\\tvNormal = normalize( transformedNormal );\\n#endif\",THREE.ShaderChunk.morphtarget_vertex,THREE.ShaderChunk.skinning_vertex,THREE.ShaderChunk.default_vertex,THREE.ShaderChunk.logdepthbuf_vertex,\"\\tvViewPosition = -mvPosition.xyz;\",THREE.ShaderChunk.worldpos_vertex,THREE.ShaderChunk.envmap_vertex,THREE.ShaderChunk.lights_phong_vertex,THREE.ShaderChunk.shadowmap_vertex,\"}\"].join(\"\\n\"),fragmentShader:[\"#define PHONG\\nuniform vec3 diffuse;\\nuniform vec3 emissive;\\nuniform vec3 specular;\\nuniform float shininess;\\nuniform float opacity;\","
                << std::endl ;
            out
                << "THREE.ShaderChunk.common,THREE.ShaderChunk.color_pars_fragment,THREE.ShaderChunk.map_pars_fragment,THREE.ShaderChunk.alphamap_pars_fragment,THREE.ShaderChunk.lightmap_pars_fragment,THREE.ShaderChunk.envmap_pars_fragment,THREE.ShaderChunk.fog_pars_fragment,THREE.ShaderChunk.lights_phong_pars_fragment,THREE.ShaderChunk.shadowmap_pars_fragment,THREE.ShaderChunk.bumpmap_pars_fragment,THREE.ShaderChunk.normalmap_pars_fragment,THREE.ShaderChunk.specularmap_pars_fragment,THREE.ShaderChunk.logdepthbuf_pars_fragment,"
                << std::endl ;
            out
                << "\"void main() {\\n\\tvec3 outgoingLight = vec3( 0.0 );\\n\\tvec4 diffuseColor = vec4( diffuse, opacity );\",THREE.ShaderChunk.logdepthbuf_fragment,THREE.ShaderChunk.map_fragment,THREE.ShaderChunk.color_fragment,THREE.ShaderChunk.alphamap_fragment,THREE.ShaderChunk.alphatest_fragment,THREE.ShaderChunk.specularmap_fragment,THREE.ShaderChunk.lights_phong_fragment,THREE.ShaderChunk.lightmap_fragment,THREE.ShaderChunk.envmap_fragment,THREE.ShaderChunk.shadowmap_fragment,THREE.ShaderChunk.linear_to_gamma_fragment,"
                << std::endl ;
            out
                << "THREE.ShaderChunk.fog_fragment,\"\\tgl_FragColor = vec4( outgoingLight, diffuseColor.a );\\n}\"].join(\"\\n\")},particle_basic:{uniforms:THREE.UniformsUtils.merge([THREE.UniformsLib.particle,THREE.UniformsLib.shadowmap]),vertexShader:[\"uniform float size;\\nuniform float scale;\",THREE.ShaderChunk.common,THREE.ShaderChunk.color_pars_vertex,THREE.ShaderChunk.shadowmap_pars_vertex,THREE.ShaderChunk.logdepthbuf_pars_vertex,\"void main() {\",THREE.ShaderChunk.color_vertex,\"\\tvec4 mvPosition = modelViewMatrix * vec4( position, 1.0 );\\n\\t#ifdef USE_SIZEATTENUATION\\n\\t\\tgl_PointSize = size * ( scale / length( mvPosition.xyz ) );\\n\\t#else\\n\\t\\tgl_PointSize = size;\\n\\t#endif\\n\\tgl_Position = projectionMatrix * mvPosition;\","
                << std::endl ;
            out
                << "THREE.ShaderChunk.logdepthbuf_vertex,THREE.ShaderChunk.worldpos_vertex,THREE.ShaderChunk.shadowmap_vertex,\"}\"].join(\"\\n\"),fragmentShader:[\"uniform vec3 psColor;\\nuniform float opacity;\",THREE.ShaderChunk.common,THREE.ShaderChunk.color_pars_fragment,THREE.ShaderChunk.map_particle_pars_fragment,THREE.ShaderChunk.fog_pars_fragment,THREE.ShaderChunk.shadowmap_pars_fragment,THREE.ShaderChunk.logdepthbuf_pars_fragment,\"void main() {\\n\\tvec3 outgoingLight = vec3( 0.0 );\\n\\tvec4 diffuseColor = vec4( psColor, opacity );\","
                << std::endl ;
            out
                << "THREE.ShaderChunk.logdepthbuf_fragment,THREE.ShaderChunk.map_particle_fragment,THREE.ShaderChunk.color_fragment,THREE.ShaderChunk.alphatest_fragment,\"\\toutgoingLight = diffuseColor.rgb;\",THREE.ShaderChunk.shadowmap_fragment,THREE.ShaderChunk.fog_fragment,\"\\tgl_FragColor = vec4( outgoingLight, diffuseColor.a );\\n}\"].join(\"\\n\")},dashed:{uniforms:THREE.UniformsUtils.merge([THREE.UniformsLib.common,THREE.UniformsLib.fog,{scale:{type:\"f\",value:1},dashSize:{type:\"f\",value:1},totalSize:{type:\"f\",value:2}}]),"
                << std::endl ;
            out
                << "vertexShader:[\"uniform float scale;\\nattribute float lineDistance;\\nvarying float vLineDistance;\",THREE.ShaderChunk.common,THREE.ShaderChunk.color_pars_vertex,THREE.ShaderChunk.logdepthbuf_pars_vertex,\"void main() {\",THREE.ShaderChunk.color_vertex,\"\\tvLineDistance = scale * lineDistance;\\n\\tvec4 mvPosition = modelViewMatrix * vec4( position, 1.0 );\\n\\tgl_Position = projectionMatrix * mvPosition;\",THREE.ShaderChunk.logdepthbuf_vertex,\"}\"].join(\"\\n\"),fragmentShader:[\"uniform vec3 diffuse;\\nuniform float opacity;\\nuniform float dashSize;\\nuniform float totalSize;\\nvarying float vLineDistance;\","
                << std::endl ;
            out
                << "THREE.ShaderChunk.common,THREE.ShaderChunk.color_pars_fragment,THREE.ShaderChunk.fog_pars_fragment,THREE.ShaderChunk.logdepthbuf_pars_fragment,\"void main() {\\n\\tif ( mod( vLineDistance, totalSize ) > dashSize ) {\\n\\t\\tdiscard;\\n\\t}\\n\\tvec3 outgoingLight = vec3( 0.0 );\\n\\tvec4 diffuseColor = vec4( diffuse, opacity );\",THREE.ShaderChunk.logdepthbuf_fragment,THREE.ShaderChunk.color_fragment,\"\\toutgoingLight = diffuseColor.rgb;\",THREE.ShaderChunk.fog_fragment,\"\\tgl_FragColor = vec4( outgoingLight, diffuseColor.a );\\n}\"].join(\"\\n\")},"
                << std::endl ;
            out
                << "depth:{uniforms:{mNear:{type:\"f\",value:1},mFar:{type:\"f\",value:2E3},opacity:{type:\"f\",value:1}},vertexShader:[THREE.ShaderChunk.common,THREE.ShaderChunk.morphtarget_pars_vertex,THREE.ShaderChunk.logdepthbuf_pars_vertex,\"void main() {\",THREE.ShaderChunk.morphtarget_vertex,THREE.ShaderChunk.default_vertex,THREE.ShaderChunk.logdepthbuf_vertex,\"}\"].join(\"\\n\"),fragmentShader:[\"uniform float mNear;\\nuniform float mFar;\\nuniform float opacity;\",THREE.ShaderChunk.common,THREE.ShaderChunk.logdepthbuf_pars_fragment,"
                << std::endl ;
            out
                << "\"void main() {\",THREE.ShaderChunk.logdepthbuf_fragment,\"\\t#ifdef USE_LOGDEPTHBUF_EXT\\n\\t\\tfloat depth = gl_FragDepthEXT / gl_FragCoord.w;\\n\\t#else\\n\\t\\tfloat depth = gl_FragCoord.z / gl_FragCoord.w;\\n\\t#endif\\n\\tfloat color = 1.0 - smoothstep( mNear, mFar, depth );\\n\\tgl_FragColor = vec4( vec3( color ), opacity );\\n}\"].join(\"\\n\")},normal:{uniforms:{opacity:{type:\"f\",value:1}},vertexShader:[\"varying vec3 vNormal;\",THREE.ShaderChunk.common,THREE.ShaderChunk.morphtarget_pars_vertex,THREE.ShaderChunk.logdepthbuf_pars_vertex,"
                << std::endl ;
            out
                << "\"void main() {\\n\\tvNormal = normalize( normalMatrix * normal );\",THREE.ShaderChunk.morphtarget_vertex,THREE.ShaderChunk.default_vertex,THREE.ShaderChunk.logdepthbuf_vertex,\"}\"].join(\"\\n\"),fragmentShader:[\"uniform float opacity;\\nvarying vec3 vNormal;\",THREE.ShaderChunk.common,THREE.ShaderChunk.logdepthbuf_pars_fragment,\"void main() {\\n\\tgl_FragColor = vec4( 0.5 * normalize( vNormal ) + 0.5, opacity );\",THREE.ShaderChunk.logdepthbuf_fragment,\"}\"].join(\"\\n\")},cube:{uniforms:{tCube:{type:\"t\",value:null},"
                << std::endl ;
            out
                << "tFlip:{type:\"f\",value:-1}},vertexShader:[\"varying vec3 vWorldPosition;\",THREE.ShaderChunk.common,THREE.ShaderChunk.logdepthbuf_pars_vertex,\"void main() {\\n\\tvWorldPosition = transformDirection( position, modelMatrix );\\n\\tgl_Position = projectionMatrix * modelViewMatrix * vec4( position, 1.0 );\",THREE.ShaderChunk.logdepthbuf_vertex,\"}\"].join(\"\\n\"),fragmentShader:[\"uniform samplerCube tCube;\\nuniform float tFlip;\\nvarying vec3 vWorldPosition;\",THREE.ShaderChunk.common,THREE.ShaderChunk.logdepthbuf_pars_fragment,"
                << std::endl ;
            out
                << "\"void main() {\\n\\tgl_FragColor = textureCube( tCube, vec3( tFlip * vWorldPosition.x, vWorldPosition.yz ) );\",THREE.ShaderChunk.logdepthbuf_fragment,\"}\"].join(\"\\n\")},equirect:{uniforms:{tEquirect:{type:\"t\",value:null},tFlip:{type:\"f\",value:-1}},vertexShader:[\"varying vec3 vWorldPosition;\",THREE.ShaderChunk.common,THREE.ShaderChunk.logdepthbuf_pars_vertex,\"void main() {\\n\\tvWorldPosition = transformDirection( position, modelMatrix );\\n\\tgl_Position = projectionMatrix * modelViewMatrix * vec4( position, 1.0 );\","
                << std::endl ;
            out
                << "THREE.ShaderChunk.logdepthbuf_vertex,\"}\"].join(\"\\n\"),fragmentShader:[\"uniform sampler2D tEquirect;\\nuniform float tFlip;\\nvarying vec3 vWorldPosition;\",THREE.ShaderChunk.common,THREE.ShaderChunk.logdepthbuf_pars_fragment,\"void main() {\\nvec3 direction = normalize( vWorldPosition );\\nvec2 sampleUV;\\nsampleUV.y = saturate( tFlip * direction.y * -0.5 + 0.5 );\\nsampleUV.x = atan( direction.z, direction.x ) * RECIPROCAL_PI2 + 0.5;\\ngl_FragColor = texture2D( tEquirect, sampleUV );\",THREE.ShaderChunk.logdepthbuf_fragment,"
                << std::endl ;
            out
                << "\"}\"].join(\"\\n\")},depthRGBA:{uniforms:{},vertexShader:[THREE.ShaderChunk.common,THREE.ShaderChunk.morphtarget_pars_vertex,THREE.ShaderChunk.skinning_pars_vertex,THREE.ShaderChunk.logdepthbuf_pars_vertex,\"void main() {\",THREE.ShaderChunk.skinbase_vertex,THREE.ShaderChunk.morphtarget_vertex,THREE.ShaderChunk.skinning_vertex,THREE.ShaderChunk.default_vertex,THREE.ShaderChunk.logdepthbuf_vertex,\"}\"].join(\"\\n\"),fragmentShader:[THREE.ShaderChunk.common,THREE.ShaderChunk.logdepthbuf_pars_fragment,\"vec4 pack_depth( const in float depth ) {\\n\\tconst vec4 bit_shift = vec4( 256.0 * 256.0 * 256.0, 256.0 * 256.0, 256.0, 1.0 );\\n\\tconst vec4 bit_mask = vec4( 0.0, 1.0 / 256.0, 1.0 / 256.0, 1.0 / 256.0 );\\n\\tvec4 res = mod( depth * bit_shift * vec4( 255 ), vec4( 256 ) ) / vec4( 255 );\\n\\tres -= res.xxyz * bit_mask;\\n\\treturn res;\\n}\\nvoid main() {\","
                << std::endl ;
            out
                << "THREE.ShaderChunk.logdepthbuf_fragment,\"\\t#ifdef USE_LOGDEPTHBUF_EXT\\n\\t\\tgl_FragData[ 0 ] = pack_depth( gl_FragDepthEXT );\\n\\t#else\\n\\t\\tgl_FragData[ 0 ] = pack_depth( gl_FragCoord.z );\\n\\t#endif\\n}\"].join(\"\\n\")}};"
                << std::endl ;
            out
                << "THREE.WebGLRenderer=function(a){function b(a){var b=a.geometry;a=a.material;var c=b.vertices.length;if(a.attributes){void 0===b.__webglCustomAttributesList&&(b.__webglCustomAttributesList=[]);for(var d in a.attributes){var e=a.attributes[d];if(!e.__webglInitialized||e.createUniqueBuffers){e.__webglInitialized=!0;var f=1;\"v2\"===e.type?f=2:\"v3\"===e.type?f=3:\"v4\"===e.type?f=4:\"c\"===e.type&&(f=3);e.size=f;e.array=new Float32Array(c*f);e.buffer=m.createBuffer();e.buffer.belongsToAttribute=d;e.needsUpdate="
                << std::endl ;
            out
                << "!0}b.__webglCustomAttributesList.push(e)}}}function c(a,b){return a.material instanceof THREE.MeshFaceMaterial?a.material.materials[b.materialIndex]:a.material}function d(a,b,c,d){c=c.attributes;var e=b.attributes;b=b.attributesKeys;for(var f=0,g=b.length;f<g;f++){var h=b[f],k=e[h];if(0<=k){var n=c[h];void 0!==n?(h=n.itemSize,m.bindBuffer(m.ARRAY_BUFFER,n.buffer),W.enableAttribute(k),m.vertexAttribPointer(k,h,m.FLOAT,!1,0,d*h*4)):void 0!==a.defaultAttributeValues&&(2===a.defaultAttributeValues[h].length?"
                << std::endl ;
            out
                << "m.vertexAttrib2fv(k,a.defaultAttributeValues[h]):3===a.defaultAttributeValues[h].length&&m.vertexAttrib3fv(k,a.defaultAttributeValues[h]))}}W.disableUnusedAttributes()}function e(a,b){return a.object.renderOrder!==b.object.renderOrder?a.object.renderOrder-b.object.renderOrder:a.material.id!==b.material.id?a.material.id-b.material.id:a.z!==b.z?a.z-b.z:a.id-b.id}function f(a,b){return a.object.renderOrder!==b.object.renderOrder?a.object.renderOrder-b.object.renderOrder:a.z!==b.z?b.z-a.z:a.id-b.id}function g(a,"
                << std::endl ;
            out
                << "b){return b[0]-a[0]}function h(a){if(!1!==a.visible){if(!(a instanceof THREE.Scene||a instanceof THREE.Group)){void 0===a.__webglInit&&(a.__webglInit=!0,a._modelViewMatrix=new THREE.Matrix4,a._normalMatrix=new THREE.Matrix3,a.addEventListener(\"removed\",wb));var c=a.geometry;if(void 0!==c&&void 0===c.__webglInit)if(c.__webglInit=!0,c.addEventListener(\"dispose\",jb),c instanceof THREE.BufferGeometry)B.info.memory.geometries++;else if(a instanceof THREE.Mesh)q(a,c);else if(a instanceof THREE.Line){if(void 0==="
                << std::endl ;
            out
                << "c.__webglVertexBuffer){c.__webglVertexBuffer=m.createBuffer();c.__webglColorBuffer=m.createBuffer();c.__webglLineDistanceBuffer=m.createBuffer();B.info.memory.geometries++;var d=c.vertices.length;c.__vertexArray=new Float32Array(3*d);c.__colorArray=new Float32Array(3*d);c.__lineDistanceArray=new Float32Array(1*d);c.__webglLineCount=d;b(a);c.verticesNeedUpdate=!0;c.colorsNeedUpdate=!0;c.lineDistancesNeedUpdate=!0}}else a instanceof THREE.PointCloud&&void 0===c.__webglVertexBuffer&&(c.__webglVertexBuffer="
                << std::endl ;
            out
                << "m.createBuffer(),c.__webglColorBuffer=m.createBuffer(),B.info.memory.geometries++,d=c.vertices.length,c.__vertexArray=new Float32Array(3*d),c.__colorArray=new Float32Array(3*d),c.__webglParticleCount=d,b(a),c.verticesNeedUpdate=!0,c.colorsNeedUpdate=!0);if(void 0===a.__webglActive)if(a.__webglActive=!0,a instanceof THREE.Mesh)if(c instanceof THREE.BufferGeometry)n(ba,c,a);else{if(c instanceof THREE.Geometry)for(var c=Ua[c.id],d=0,e=c.length;d<e;d++)n(ba,c[d],a)}else a instanceof THREE.Line||a instanceof"
                << std::endl ;
            out
                << "THREE.PointCloud?n(ba,c,a):(a instanceof THREE.ImmediateRenderObject||a.immediateRenderCallback)&&qa.push({id:null,object:a,opaque:null,transparent:null,z:0});if(a instanceof THREE.Light)ca.push(a);else if(a instanceof THREE.Sprite)Xa.push(a);else if(a instanceof THREE.LensFlare)Ya.push(a);else if((c=ba[a.id])&&(!1===a.frustumCulled||!0===cb.intersectsObject(a)))for(d=0,e=c.length;d<e;d++){var f=c[d],g=f,k=g.object,l=g.buffer,p=k.geometry,k=k.material;k instanceof THREE.MeshFaceMaterial?(k=k.materials[p instanceof"
                << std::endl ;
            out
                << "THREE.BufferGeometry?0:l.materialIndex],g.material=k,k.transparent?Qa.push(g):Ka.push(g)):k&&(g.material=k,k.transparent?Qa.push(g):Ka.push(g));f.render=!0;!0===B.sortObjects&&(wa.setFromMatrixPosition(a.matrixWorld),wa.applyProjection(db),f.z=wa.z)}}d=0;for(e=a.children.length;d<e;d++)h(a.children[d])}}function k(a,b,c,d,e){for(var f,g=0,h=a.length;g<h;g++){f=a[g];var k=f.object,m=f.buffer;w(k,b);if(e)f=e;else{f=f.material;if(!f)continue;u(f)}B.setMaterialFaces(f);m instanceof THREE.BufferGeometry?"
                << std::endl ;
            out
                << "B.renderBufferDirect(b,c,d,f,m,k):B.renderBuffer(b,c,d,f,m,k)}}function l(a,b,c,d,e,f){for(var g,h=0,k=a.length;h<k;h++){g=a[h];var m=g.object;if(m.visible){if(f)g=f;else{g=g[b];if(!g)continue;u(g)}B.renderImmediateObject(c,d,e,g,m)}}}function p(a){var b=a.object.material;b.transparent?(a.transparent=b,a.opaque=null):(a.opaque=b,a.transparent=null)}function q(a,b){var d=a.material,e=!1;if(void 0===Ua[b.id]||!0===b.groupsNeedUpdate){delete ba[a.id];for(var f=Ua,g=b.id,d=d instanceof THREE.MeshFaceMaterial,"
                << std::endl ;
            out
                << "h=da.get(\"OES_element_index_uint\")?4294967296:65535,k,e={},l=b.morphTargets.length,p=b.morphNormals.length,q,s={},t=[],r=0,w=b.faces.length;r<w;r++){k=b.faces[r];var u=d?k.materialIndex:0;u in e||(e[u]={hash:u,counter:0});k=e[u].hash+\"_\"+e[u].counter;k in s||(q={id:Qb++,faces3:[],materialIndex:u,vertices:0,numMorphTargets:l,numMorphNormals:p},s[k]=q,t.push(q));s[k].vertices+3>h&&(e[u].counter+=1,k=e[u].hash+\"_\"+e[u].counter,k in s||(q={id:Qb++,faces3:[],materialIndex:u,vertices:0,numMorphTargets:l,"
                << std::endl ;
            out
                << "numMorphNormals:p},s[k]=q,t.push(q)));s[k].faces3.push(r);s[k].vertices+=3}f[g]=t;b.groupsNeedUpdate=!1}f=Ua[b.id];g=0;for(d=f.length;g<d;g++){h=f[g];if(void 0===h.__webglVertexBuffer){e=h;e.__webglVertexBuffer=m.createBuffer();e.__webglNormalBuffer=m.createBuffer();e.__webglTangentBuffer=m.createBuffer();e.__webglColorBuffer=m.createBuffer();e.__webglUVBuffer=m.createBuffer();e.__webglUV2Buffer=m.createBuffer();e.__webglSkinIndicesBuffer=m.createBuffer();e.__webglSkinWeightsBuffer=m.createBuffer();"
                << std::endl ;
            out
                << "e.__webglFaceBuffer=m.createBuffer();e.__webglLineBuffer=m.createBuffer();if(p=e.numMorphTargets)for(e.__webglMorphTargetsBuffers=[],l=0;l<p;l++)e.__webglMorphTargetsBuffers.push(m.createBuffer());if(p=e.numMorphNormals)for(e.__webglMorphNormalsBuffers=[],l=0;l<p;l++)e.__webglMorphNormalsBuffers.push(m.createBuffer());B.info.memory.geometries++;e=h;r=a;w=r.geometry;p=e.faces3;l=3*p.length;s=1*p.length;t=3*p.length;p=c(r,e);e.__vertexArray=new Float32Array(3*l);e.__normalArray=new Float32Array(3*l);"
                << std::endl ;
            out
                << "e.__colorArray=new Float32Array(3*l);e.__uvArray=new Float32Array(2*l);1<w.faceVertexUvs.length&&(e.__uv2Array=new Float32Array(2*l));w.hasTangents&&(e.__tangentArray=new Float32Array(4*l));r.geometry.skinWeights.length&&r.geometry.skinIndices.length&&(e.__skinIndexArray=new Float32Array(4*l),e.__skinWeightArray=new Float32Array(4*l));r=null!==da.get(\"OES_element_index_uint\")&&21845<s?Uint32Array:Uint16Array;e.__typeArray=r;e.__faceArray=new r(3*s);e.__lineArray=new r(2*t);if(w=e.numMorphTargets)for(e.__morphTargetsArrays="
                << std::endl ;
            out
                << "[],r=0;r<w;r++)e.__morphTargetsArrays.push(new Float32Array(3*l));if(w=e.numMorphNormals)for(e.__morphNormalsArrays=[],r=0;r<w;r++)e.__morphNormalsArrays.push(new Float32Array(3*l));e.__webglFaceCount=3*s;e.__webglLineCount=2*t;if(p.attributes)for(s in void 0===e.__webglCustomAttributesList&&(e.__webglCustomAttributesList=[]),s=void 0,p.attributes){var t=p.attributes[s],r={},v;for(v in t)r[v]=t[v];if(!r.__webglInitialized||r.createUniqueBuffers)r.__webglInitialized=!0,w=1,\"v2\"===r.type?w=2:\"v3\"==="
                << std::endl ;
            out
                << "r.type?w=3:\"v4\"===r.type?w=4:\"c\"===r.type&&(w=3),r.size=w,r.array=new Float32Array(l*w),r.buffer=m.createBuffer(),r.buffer.belongsToAttribute=s,t.needsUpdate=!0,r.__original=t;e.__webglCustomAttributesList.push(r)}e.__inittedArrays=!0;b.verticesNeedUpdate=!0;b.morphTargetsNeedUpdate=!0;b.elementsNeedUpdate=!0;b.uvsNeedUpdate=!0;b.normalsNeedUpdate=!0;b.tangentsNeedUpdate=!0;e=b.colorsNeedUpdate=!0}else e=!1;(e||void 0===a.__webglActive)&&n(ba,h,a)}a.__webglActive=!0}function n(a,b,c){var d=c.id;a[d]="
                << std::endl ;
            out
                << "a[d]||[];a[d].push({id:d,buffer:b,object:c,material:null,z:0})}function t(a){var b=a.geometry;if(b instanceof THREE.BufferGeometry)for(var d=b.attributes,e=b.attributesKeys,f=0,g=e.length;f<g;f++){var h=e[f],k=d[h],n=\"index\"===h?m.ELEMENT_ARRAY_BUFFER:m.ARRAY_BUFFER;void 0===k.buffer?(k.buffer=m.createBuffer(),m.bindBuffer(n,k.buffer),m.bufferData(n,k.array,k instanceof THREE.DynamicBufferAttribute?m.DYNAMIC_DRAW:m.STATIC_DRAW),k.needsUpdate=!1):!0===k.needsUpdate&&(m.bindBuffer(n,k.buffer),void 0==="
                << std::endl ;
            out
                << "k.updateRange||-1===k.updateRange.count?m.bufferSubData(n,0,k.array):0===k.updateRange.count?console.error(\"THREE.WebGLRenderer.updateObject: using updateRange for THREE.DynamicBufferAttribute and marked as needsUpdate but count is 0, ensure you are using set methods or updating manually.\"):(m.bufferSubData(n,k.updateRange.offset*k.array.BYTES_PER_ELEMENT,k.array.subarray(k.updateRange.offset,k.updateRange.offset+k.updateRange.count)),k.updateRange.count=0),k.needsUpdate=!1)}else if(a instanceof THREE.Mesh){!0==="
                << std::endl ;
            out
                << "b.groupsNeedUpdate&&q(a,b);for(var l=Ua[b.id],f=0,p=l.length;f<p;f++){var t=l[f],w=c(a,t),u=w.attributes&&r(w);if(b.verticesNeedUpdate||b.morphTargetsNeedUpdate||b.elementsNeedUpdate||b.uvsNeedUpdate||b.normalsNeedUpdate||b.colorsNeedUpdate||b.tangentsNeedUpdate||u){var v=t,x=a,D=m.DYNAMIC_DRAW,A=!b.dynamic,E=w;if(v.__inittedArrays){var G=!1===E instanceof THREE.MeshPhongMaterial&&E.shading===THREE.FlatShading,y=void 0,z=void 0,F=void 0,B=void 0,I=void 0,H=void 0,M=void 0,R=void 0,P=void 0,U=void 0,"
                << std::endl ;
            out
                << "O=void 0,J=void 0,L=void 0,N=void 0,Ka=void 0,V=void 0,W=void 0,Qa=void 0,Ya=void 0,Xa=void 0,da=void 0,ba=void 0,ja=void 0,Pa=void 0,ka=void 0,Q=void 0,ha=void 0,ia=void 0,ob=void 0,Y=void 0,ub=void 0,pa=void 0,ab=void 0,oa=void 0,ca=void 0,qa=void 0,Ca=void 0,ta=void 0,na=void 0,wa=void 0,La=0,Ma=0,kb=0,yb=0,zb=0,Ra=0,Aa=0,eb=0,Ha=0,la=0,ra=0,K=0,za=void 0,Sa=v.__vertexArray,Ab=v.__uvArray,lb=v.__uv2Array,Na=v.__normalArray,sa=v.__tangentArray,Da=v.__colorArray,Ea=v.__skinIndexArray,Fa=v.__skinWeightArray,"
                << std::endl ;
            out
                << "Gb=v.__morphTargetsArrays,Bb=v.__morphNormalsArrays,mb=v.__webglCustomAttributesList,C=void 0,Va=v.__faceArray,Ta=v.__lineArray,ea=x.geometry,fb=ea.elementsNeedUpdate,vb=ea.uvsNeedUpdate,Mb=ea.normalsNeedUpdate,Ob=ea.tangentsNeedUpdate,ib=ea.colorsNeedUpdate,sb=ea.morphTargetsNeedUpdate,Cb=ea.vertices,$=v.faces3,xa=ea.faces,Hb=ea.faceVertexUvs[0],Oa=ea.faceVertexUvs[1],$a=ea.skinIndices,Ga=ea.skinWeights,nb=ea.morphTargets,bb=ea.morphNormals;if(ea.verticesNeedUpdate){y=0;for(z=$.length;y<z;y++)B="
                << std::endl ;
            out
                << "xa[$[y]],J=Cb[B.a],L=Cb[B.b],N=Cb[B.c],Sa[Ma]=J.x,Sa[Ma+1]=J.y,Sa[Ma+2]=J.z,Sa[Ma+3]=L.x,Sa[Ma+4]=L.y,Sa[Ma+5]=L.z,Sa[Ma+6]=N.x,Sa[Ma+7]=N.y,Sa[Ma+8]=N.z,Ma+=9;m.bindBuffer(m.ARRAY_BUFFER,v.__webglVertexBuffer);m.bufferData(m.ARRAY_BUFFER,Sa,D)}if(sb)for(ca=0,qa=nb.length;ca<qa;ca++){y=ra=0;for(z=$.length;y<z;y++)na=$[y],B=xa[na],J=nb[ca].vertices[B.a],L=nb[ca].vertices[B.b],N=nb[ca].vertices[B.c],Ca=Gb[ca],Ca[ra]=J.x,Ca[ra+1]=J.y,Ca[ra+2]=J.z,Ca[ra+3]=L.x,Ca[ra+4]=L.y,Ca[ra+5]=L.z,Ca[ra+6]=N.x,Ca[ra+"
                << std::endl ;
            out
                << "7]=N.y,Ca[ra+8]=N.z,E.morphNormals&&(G?Xa=Ya=Qa=bb[ca].faceNormals[na]:(wa=bb[ca].vertexNormals[na],Qa=wa.a,Ya=wa.b,Xa=wa.c),ta=Bb[ca],ta[ra]=Qa.x,ta[ra+1]=Qa.y,ta[ra+2]=Qa.z,ta[ra+3]=Ya.x,ta[ra+4]=Ya.y,ta[ra+5]=Ya.z,ta[ra+6]=Xa.x,ta[ra+7]=Xa.y,ta[ra+8]=Xa.z),ra+=9;m.bindBuffer(m.ARRAY_BUFFER,v.__webglMorphTargetsBuffers[ca]);m.bufferData(m.ARRAY_BUFFER,Gb[ca],D);E.morphNormals&&(m.bindBuffer(m.ARRAY_BUFFER,v.__webglMorphNormalsBuffers[ca]),m.bufferData(m.ARRAY_BUFFER,Bb[ca],D))}if(Ga.length){y=0;"
                << std::endl ;
            out
                << "for(z=$.length;y<z;y++)B=xa[$[y]],Pa=Ga[B.a],ka=Ga[B.b],Q=Ga[B.c],Fa[la]=Pa.x,Fa[la+1]=Pa.y,Fa[la+2]=Pa.z,Fa[la+3]=Pa.w,Fa[la+4]=ka.x,Fa[la+5]=ka.y,Fa[la+6]=ka.z,Fa[la+7]=ka.w,Fa[la+8]=Q.x,Fa[la+9]=Q.y,Fa[la+10]=Q.z,Fa[la+11]=Q.w,ha=$a[B.a],ia=$a[B.b],ob=$a[B.c],Ea[la]=ha.x,Ea[la+1]=ha.y,Ea[la+2]=ha.z,Ea[la+3]=ha.w,Ea[la+4]=ia.x,Ea[la+5]=ia.y,Ea[la+6]=ia.z,Ea[la+7]=ia.w,Ea[la+8]=ob.x,Ea[la+9]=ob.y,Ea[la+10]=ob.z,Ea[la+11]=ob.w,la+=12;0<la&&(m.bindBuffer(m.ARRAY_BUFFER,v.__webglSkinIndicesBuffer),"
                << std::endl ;
            out
                << "m.bufferData(m.ARRAY_BUFFER,Ea,D),m.bindBuffer(m.ARRAY_BUFFER,v.__webglSkinWeightsBuffer),m.bufferData(m.ARRAY_BUFFER,Fa,D))}if(ib){y=0;for(z=$.length;y<z;y++)B=xa[$[y]],M=B.vertexColors,R=B.color,3===M.length&&E.vertexColors===THREE.VertexColors?(da=M[0],ba=M[1],ja=M[2]):ja=ba=da=R,Da[Ha]=da.r,Da[Ha+1]=da.g,Da[Ha+2]=da.b,Da[Ha+3]=ba.r,Da[Ha+4]=ba.g,Da[Ha+5]=ba.b,Da[Ha+6]=ja.r,Da[Ha+7]=ja.g,Da[Ha+8]=ja.b,Ha+=9;0<Ha&&(m.bindBuffer(m.ARRAY_BUFFER,v.__webglColorBuffer),m.bufferData(m.ARRAY_BUFFER,Da,"
                << std::endl ;
            out
                << "D))}if(Ob&&ea.hasTangents){y=0;for(z=$.length;y<z;y++)B=xa[$[y]],P=B.vertexTangents,Ka=P[0],V=P[1],W=P[2],sa[Aa]=Ka.x,sa[Aa+1]=Ka.y,sa[Aa+2]=Ka.z,sa[Aa+3]=Ka.w,sa[Aa+4]=V.x,sa[Aa+5]=V.y,sa[Aa+6]=V.z,sa[Aa+7]=V.w,sa[Aa+8]=W.x,sa[Aa+9]=W.y,sa[Aa+10]=W.z,sa[Aa+11]=W.w,Aa+=12;m.bindBuffer(m.ARRAY_BUFFER,v.__webglTangentBuffer);m.bufferData(m.ARRAY_BUFFER,sa,D)}if(Mb){y=0;for(z=$.length;y<z;y++)if(B=xa[$[y]],I=B.vertexNormals,H=B.normal,3===I.length&&!1===G)for(Y=0;3>Y;Y++)pa=I[Y],Na[Ra]=pa.x,Na[Ra+1]="
                << std::endl ;
            out
                << "pa.y,Na[Ra+2]=pa.z,Ra+=3;else for(Y=0;3>Y;Y++)Na[Ra]=H.x,Na[Ra+1]=H.y,Na[Ra+2]=H.z,Ra+=3;m.bindBuffer(m.ARRAY_BUFFER,v.__webglNormalBuffer);m.bufferData(m.ARRAY_BUFFER,Na,D)}if(vb&&Hb){y=0;for(z=$.length;y<z;y++)if(F=$[y],U=Hb[F],void 0!==U)for(Y=0;3>Y;Y++)ab=U[Y],Ab[kb]=ab.x,Ab[kb+1]=ab.y,kb+=2;0<kb&&(m.bindBuffer(m.ARRAY_BUFFER,v.__webglUVBuffer),m.bufferData(m.ARRAY_BUFFER,Ab,D))}if(vb&&Oa){y=0;for(z=$.length;y<z;y++)if(F=$[y],O=Oa[F],void 0!==O)for(Y=0;3>Y;Y++)oa=O[Y],lb[yb]=oa.x,lb[yb+1]=oa.y,"
                << std::endl ;
            out
                << "yb+=2;0<yb&&(m.bindBuffer(m.ARRAY_BUFFER,v.__webglUV2Buffer),m.bufferData(m.ARRAY_BUFFER,lb,D))}if(fb){y=0;for(z=$.length;y<z;y++)Va[zb]=La,Va[zb+1]=La+1,Va[zb+2]=La+2,zb+=3,Ta[eb]=La,Ta[eb+1]=La+1,Ta[eb+2]=La,Ta[eb+3]=La+2,Ta[eb+4]=La+1,Ta[eb+5]=La+2,eb+=6,La+=3;m.bindBuffer(m.ELEMENT_ARRAY_BUFFER,v.__webglFaceBuffer);m.bufferData(m.ELEMENT_ARRAY_BUFFER,Va,D);m.bindBuffer(m.ELEMENT_ARRAY_BUFFER,v.__webglLineBuffer);m.bufferData(m.ELEMENT_ARRAY_BUFFER,Ta,D)}if(mb)for(Y=0,ub=mb.length;Y<ub;Y++)if(C="
                << std::endl ;
            out
                << "mb[Y],C.__original.needsUpdate){K=0;if(1===C.size)if(void 0===C.boundTo||\"vertices\"===C.boundTo)for(y=0,z=$.length;y<z;y++)B=xa[$[y]],C.array[K]=C.value[B.a],C.array[K+1]=C.value[B.b],C.array[K+2]=C.value[B.c],K+=3;else{if(\"faces\"===C.boundTo)for(y=0,z=$.length;y<z;y++)za=C.value[$[y]],C.array[K]=za,C.array[K+1]=za,C.array[K+2]=za,K+=3}else if(2===C.size)if(void 0===C.boundTo||\"vertices\"===C.boundTo)for(y=0,z=$.length;y<z;y++)B=xa[$[y]],J=C.value[B.a],L=C.value[B.b],N=C.value[B.c],C.array[K]=J.x,"
                << std::endl ;
            out
                << "C.array[K+1]=J.y,C.array[K+2]=L.x,C.array[K+3]=L.y,C.array[K+4]=N.x,C.array[K+5]=N.y,K+=6;else{if(\"faces\"===C.boundTo)for(y=0,z=$.length;y<z;y++)N=L=J=za=C.value[$[y]],C.array[K]=J.x,C.array[K+1]=J.y,C.array[K+2]=L.x,C.array[K+3]=L.y,C.array[K+4]=N.x,C.array[K+5]=N.y,K+=6}else if(3===C.size){var T;T=\"c\"===C.type?[\"r\",\"g\",\"b\"]:[\"x\",\"y\",\"z\"];if(void 0===C.boundTo||\"vertices\"===C.boundTo)for(y=0,z=$.length;y<z;y++)B=xa[$[y]],J=C.value[B.a],L=C.value[B.b],N=C.value[B.c],C.array[K]=J[T[0]],C.array[K+1]="
                << std::endl ;
            out
                << "J[T[1]],C.array[K+2]=J[T[2]],C.array[K+3]=L[T[0]],C.array[K+4]=L[T[1]],C.array[K+5]=L[T[2]],C.array[K+6]=N[T[0]],C.array[K+7]=N[T[1]],C.array[K+8]=N[T[2]],K+=9;else if(\"faces\"===C.boundTo)for(y=0,z=$.length;y<z;y++)N=L=J=za=C.value[$[y]],C.array[K]=J[T[0]],C.array[K+1]=J[T[1]],C.array[K+2]=J[T[2]],C.array[K+3]=L[T[0]],C.array[K+4]=L[T[1]],C.array[K+5]=L[T[2]],C.array[K+6]=N[T[0]],C.array[K+7]=N[T[1]],C.array[K+8]=N[T[2]],K+=9;else if(\"faceVertices\"===C.boundTo)for(y=0,z=$.length;y<z;y++)za=C.value[$[y]],"
                << std::endl ;
            out
                << "J=za[0],L=za[1],N=za[2],C.array[K]=J[T[0]],C.array[K+1]=J[T[1]],C.array[K+2]=J[T[2]],C.array[K+3]=L[T[0]],C.array[K+4]=L[T[1]],C.array[K+5]=L[T[2]],C.array[K+6]=N[T[0]],C.array[K+7]=N[T[1]],C.array[K+8]=N[T[2]],K+=9}else if(4===C.size)if(void 0===C.boundTo||\"vertices\"===C.boundTo)for(y=0,z=$.length;y<z;y++)B=xa[$[y]],J=C.value[B.a],L=C.value[B.b],N=C.value[B.c],C.array[K]=J.x,C.array[K+1]=J.y,C.array[K+2]=J.z,C.array[K+3]=J.w,C.array[K+4]=L.x,C.array[K+5]=L.y,C.array[K+6]=L.z,C.array[K+7]=L.w,C.array[K+"
                << std::endl ;
            out
                << "8]=N.x,C.array[K+9]=N.y,C.array[K+10]=N.z,C.array[K+11]=N.w,K+=12;else if(\"faces\"===C.boundTo)for(y=0,z=$.length;y<z;y++)N=L=J=za=C.value[$[y]],C.array[K]=J.x,C.array[K+1]=J.y,C.array[K+2]=J.z,C.array[K+3]=J.w,C.array[K+4]=L.x,C.array[K+5]=L.y,C.array[K+6]=L.z,C.array[K+7]=L.w,C.array[K+8]=N.x,C.array[K+9]=N.y,C.array[K+10]=N.z,C.array[K+11]=N.w,K+=12;else if(\"faceVertices\"===C.boundTo)for(y=0,z=$.length;y<z;y++)za=C.value[$[y]],J=za[0],L=za[1],N=za[2],C.array[K]=J.x,C.array[K+1]=J.y,C.array[K+2]="
                << std::endl ;
            out
                << "J.z,C.array[K+3]=J.w,C.array[K+4]=L.x,C.array[K+5]=L.y,C.array[K+6]=L.z,C.array[K+7]=L.w,C.array[K+8]=N.x,C.array[K+9]=N.y,C.array[K+10]=N.z,C.array[K+11]=N.w,K+=12;m.bindBuffer(m.ARRAY_BUFFER,C.buffer);m.bufferData(m.ARRAY_BUFFER,C.array,D)}A&&(delete v.__inittedArrays,delete v.__colorArray,delete v.__normalArray,delete v.__tangentArray,delete v.__uvArray,delete v.__uv2Array,delete v.__faceArray,delete v.__vertexArray,delete v.__lineArray,delete v.__skinIndexArray,delete v.__skinWeightArray)}}}b.verticesNeedUpdate="
                << std::endl ;
            out
                << "!1;b.morphTargetsNeedUpdate=!1;b.elementsNeedUpdate=!1;b.uvsNeedUpdate=!1;b.normalsNeedUpdate=!1;b.colorsNeedUpdate=!1;b.tangentsNeedUpdate=!1;w.attributes&&s(w)}else if(a instanceof THREE.Line){w=c(a,b);u=w.attributes&&r(w);if(b.verticesNeedUpdate||b.colorsNeedUpdate||b.lineDistancesNeedUpdate||u){var Db=m.DYNAMIC_DRAW,S,aa,Z,Ba,X,Eb,Rb=b.vertices,Ib=b.colors,gb=b.lineDistances,ya=Rb.length,pb=Ib.length,qb=gb.length,Wa=b.__vertexArray,tb=b.__colorArray,hb=b.__lineDistanceArray,$b=b.colorsNeedUpdate,"
                << std::endl ;
            out
                << "Fb=b.lineDistancesNeedUpdate,Sb=b.__webglCustomAttributesList,Jb,cb,ua,Kb,Ia,fa;if(b.verticesNeedUpdate){for(S=0;S<ya;S++)Ba=Rb[S],X=3*S,Wa[X]=Ba.x,Wa[X+1]=Ba.y,Wa[X+2]=Ba.z;m.bindBuffer(m.ARRAY_BUFFER,b.__webglVertexBuffer);m.bufferData(m.ARRAY_BUFFER,Wa,Db)}if($b){for(aa=0;aa<pb;aa++)Eb=Ib[aa],X=3*aa,tb[X]=Eb.r,tb[X+1]=Eb.g,tb[X+2]=Eb.b;m.bindBuffer(m.ARRAY_BUFFER,b.__webglColorBuffer);m.bufferData(m.ARRAY_BUFFER,tb,Db)}if(Fb){for(Z=0;Z<qb;Z++)hb[Z]=gb[Z];m.bindBuffer(m.ARRAY_BUFFER,b.__webglLineDistanceBuffer);"
                << std::endl ;
            out
                << "m.bufferData(m.ARRAY_BUFFER,hb,Db)}if(Sb)for(Jb=0,cb=Sb.length;Jb<cb;Jb++)if(fa=Sb[Jb],fa.needsUpdate&&(void 0===fa.boundTo||\"vertices\"===fa.boundTo)){X=0;Kb=fa.value.length;if(1===fa.size)for(ua=0;ua<Kb;ua++)fa.array[ua]=fa.value[ua];else if(2===fa.size)for(ua=0;ua<Kb;ua++)Ia=fa.value[ua],fa.array[X]=Ia.x,fa.array[X+1]=Ia.y,X+=2;else if(3===fa.size)if(\"c\"===fa.type)for(ua=0;ua<Kb;ua++)Ia=fa.value[ua],fa.array[X]=Ia.r,fa.array[X+1]=Ia.g,fa.array[X+2]=Ia.b,X+=3;else for(ua=0;ua<Kb;ua++)Ia=fa.value[ua],"
                << std::endl ;
            out
                << "fa.array[X]=Ia.x,fa.array[X+1]=Ia.y,fa.array[X+2]=Ia.z,X+=3;else if(4===fa.size)for(ua=0;ua<Kb;ua++)Ia=fa.value[ua],fa.array[X]=Ia.x,fa.array[X+1]=Ia.y,fa.array[X+2]=Ia.z,fa.array[X+3]=Ia.w,X+=4;m.bindBuffer(m.ARRAY_BUFFER,fa.buffer);m.bufferData(m.ARRAY_BUFFER,fa.array,Db);fa.needsUpdate=!1}}b.verticesNeedUpdate=!1;b.colorsNeedUpdate=!1;b.lineDistancesNeedUpdate=!1;w.attributes&&s(w)}else if(a instanceof THREE.PointCloud){w=c(a,b);u=w.attributes&&r(w);if(b.verticesNeedUpdate||b.colorsNeedUpdate||"
                << std::endl ;
            out
                << "u){var db=m.DYNAMIC_DRAW,Tb,Ub,ac,ma,bc,Nb=b.vertices,Vb=Nb.length,Pb=b.colors,rb=Pb.length,cc=b.__vertexArray,dc=b.__colorArray,wb=b.colorsNeedUpdate,gc=b.__webglCustomAttributesList,ec,jb,va,Lb,Ja,ga;if(b.verticesNeedUpdate){for(Tb=0;Tb<Vb;Tb++)ac=Nb[Tb],ma=3*Tb,cc[ma]=ac.x,cc[ma+1]=ac.y,cc[ma+2]=ac.z;m.bindBuffer(m.ARRAY_BUFFER,b.__webglVertexBuffer);m.bufferData(m.ARRAY_BUFFER,cc,db)}if(wb){for(Ub=0;Ub<rb;Ub++)bc=Pb[Ub],ma=3*Ub,dc[ma]=bc.r,dc[ma+1]=bc.g,dc[ma+2]=bc.b;m.bindBuffer(m.ARRAY_BUFFER,"
                << std::endl ;
            out
                << "b.__webglColorBuffer);m.bufferData(m.ARRAY_BUFFER,dc,db)}if(gc)for(ec=0,jb=gc.length;ec<jb;ec++){ga=gc[ec];if(ga.needsUpdate&&(void 0===ga.boundTo||\"vertices\"===ga.boundTo))if(Lb=ga.value.length,ma=0,1===ga.size)for(va=0;va<Lb;va++)ga.array[va]=ga.value[va];else if(2===ga.size)for(va=0;va<Lb;va++)Ja=ga.value[va],ga.array[ma]=Ja.x,ga.array[ma+1]=Ja.y,ma+=2;else if(3===ga.size)if(\"c\"===ga.type)for(va=0;va<Lb;va++)Ja=ga.value[va],ga.array[ma]=Ja.r,ga.array[ma+1]=Ja.g,ga.array[ma+2]=Ja.b,ma+=3;else for(va="
                << std::endl ;
            out
                << "0;va<Lb;va++)Ja=ga.value[va],ga.array[ma]=Ja.x,ga.array[ma+1]=Ja.y,ga.array[ma+2]=Ja.z,ma+=3;else if(4===ga.size)for(va=0;va<Lb;va++)Ja=ga.value[va],ga.array[ma]=Ja.x,ga.array[ma+1]=Ja.y,ga.array[ma+2]=Ja.z,ga.array[ma+3]=Ja.w,ma+=4;m.bindBuffer(m.ARRAY_BUFFER,ga.buffer);m.bufferData(m.ARRAY_BUFFER,ga.array,db);ga.needsUpdate=!1}}b.verticesNeedUpdate=!1;b.colorsNeedUpdate=!1;w.attributes&&s(w)}}function r(a){for(var b in a.attributes)if(a.attributes[b].needsUpdate)return!0;return!1}function s(a){for(var b in a.attributes)a.attributes[b].needsUpdate="
                << std::endl ;
            out
                << "!1}function u(a){!0===a.transparent?W.setBlending(a.blending,a.blendEquation,a.blendSrc,a.blendDst,a.blendEquationAlpha,a.blendSrcAlpha,a.blendDstAlpha):W.setBlending(THREE.NoBlending);W.setDepthTest(a.depthTest);W.setDepthWrite(a.depthWrite);W.setColorWrite(a.colorWrite);W.setPolygonOffset(a.polygonOffset,a.polygonOffsetFactor,a.polygonOffsetUnits)}function v(a,b,c,d,e){var f,g,h,k;Mb=0;if(d.needsUpdate){d.program&&hc(d);d.addEventListener(\"dispose\",ic);var n=pc[d.type];if(n){var l=THREE.ShaderLib[n];"
                << std::endl ;
            out
                << "d.__webglShader={uniforms:THREE.UniformsUtils.clone(l.uniforms),vertexShader:l.vertexShader,fragmentShader:l.fragmentShader}}else d.__webglShader={uniforms:d.uniforms,vertexShader:d.vertexShader,fragmentShader:d.fragmentShader};for(var p=0,q=0,r=0,s=0,t=0,w=b.length;t<w;t++){var v=b[t];v.onlyShadow||!1===v.visible||(v instanceof THREE.DirectionalLight&&p++,v instanceof THREE.PointLight&&q++,v instanceof THREE.SpotLight&&r++,v instanceof THREE.HemisphereLight&&s++)}f=p;g=q;h=r;k=s;for(var u,z=0,G="
                << std::endl ;
            out
                << "0,F=b.length;G<F;G++){var J=b[G];J.castShadow&&(J instanceof THREE.SpotLight&&z++,J instanceof THREE.DirectionalLight&&!J.shadowCascade&&z++)}u=z;var H;if(Nb&&e&&e.skeleton&&e.skeleton.useVertexTexture)H=1024;else{var N=m.getParameter(m.MAX_VERTEX_UNIFORM_VECTORS),M=Math.floor((N-20)/4);void 0!==e&&e instanceof THREE.SkinnedMesh&&(M=Math.min(e.skeleton.bones.length,M),M<e.skeleton.bones.length&&THREE.warn(\"WebGLRenderer: too many bones - \"+e.skeleton.bones.length+\", this GPU supports just \"+M+\" (try OpenGL instead of ANGLE)\"));"
                << std::endl ;
            out
                << "H=M}var P={precision:L,supportsVertexTextures:Vb,map:!!d.map,envMap:!!d.envMap,envMapMode:d.envMap&&d.envMap.mapping,lightMap:!!d.lightMap,bumpMap:!!d.bumpMap,normalMap:!!d.normalMap,specularMap:!!d.specularMap,alphaMap:!!d.alphaMap,combine:d.combine,vertexColors:d.vertexColors,fog:c,useFog:d.fog,fogExp:c instanceof THREE.FogExp2,flatShading:d.shading===THREE.FlatShading,sizeAttenuation:d.sizeAttenuation,logarithmicDepthBuffer:ja,skinning:d.skinning,maxBones:H,useVertexTexture:Nb&&e&&e.skeleton&&"
                << std::endl ;
            out
                << "e.skeleton.useVertexTexture,morphTargets:d.morphTargets,morphNormals:d.morphNormals,maxMorphTargets:B.maxMorphTargets,maxMorphNormals:B.maxMorphNormals,maxDirLights:f,maxPointLights:g,maxSpotLights:h,maxHemiLights:k,maxShadows:u,shadowMapEnabled:B.shadowMapEnabled&&e.receiveShadow&&0<u,shadowMapType:B.shadowMapType,shadowMapDebug:B.shadowMapDebug,shadowMapCascade:B.shadowMapCascade,alphaTest:d.alphaTest,metal:d.metal,wrapAround:d.wrapAround,doubleSided:d.side===THREE.DoubleSide,flipSided:d.side==="
                << std::endl ;
            out
                << "THREE.BackSide},R=[];n?R.push(n):(R.push(d.fragmentShader),R.push(d.vertexShader));if(void 0!==d.defines)for(var O in d.defines)R.push(O),R.push(d.defines[O]);for(O in P)R.push(O),R.push(P[O]);for(var Ka=R.join(),V,W=0,Qa=Pa.length;W<Qa;W++){var Ya=Pa[W];if(Ya.code===Ka){V=Ya;V.usedTimes++;break}}void 0===V&&(V=new THREE.WebGLProgram(B,Ka,d,P),Pa.push(V),B.info.memory.programs=Pa.length);d.program=V;var Xa=V.attributes;if(d.morphTargets){d.numSupportedMorphTargets=0;for(var ca,da=\"morphTarget\",ba="
                << std::endl ;
            out
                << "0;ba<B.maxMorphTargets;ba++)ca=da+ba,0<=Xa[ca]&&d.numSupportedMorphTargets++}if(d.morphNormals)for(d.numSupportedMorphNormals=0,da=\"morphNormal\",ba=0;ba<B.maxMorphNormals;ba++)ca=da+ba,0<=Xa[ca]&&d.numSupportedMorphNormals++;d.uniformsList=[];for(var ha in d.__webglShader.uniforms){var ta=d.program.uniforms[ha];ta&&d.uniformsList.push([d.__webglShader.uniforms[ha],ta])}d.needsUpdate=!1}d.morphTargets&&!e.__webglMorphTargetInfluences&&(e.__webglMorphTargetInfluences=new Float32Array(B.maxMorphTargets));"
                << std::endl ;
            out
                << "var ab=!1,oa=!1,qa=!1,Ua=d.program,ka=Ua.uniforms,Q=d.__webglShader.uniforms;Ua.id!==ob&&(m.useProgram(Ua.program),ob=Ua.id,qa=oa=ab=!0);d.id!==ub&&(-1===ub&&(qa=!0),ub=d.id,oa=!0);if(ab||a!==vb)m.uniformMatrix4fv(ka.projectionMatrix,!1,a.projectionMatrix.elements),ja&&m.uniform1f(ka.logDepthBufFC,2/(Math.log(a.far+1)/Math.LN2)),a!==vb&&(vb=a),(d instanceof THREE.ShaderMaterial||d instanceof THREE.MeshPhongMaterial||d.envMap)&&null!==ka.cameraPosition&&(wa.setFromMatrixPosition(a.matrixWorld),m.uniform3f(ka.cameraPosition,"
                << std::endl ;
            out
                << "wa.x,wa.y,wa.z)),(d instanceof THREE.MeshPhongMaterial||d instanceof THREE.MeshLambertMaterial||d instanceof THREE.MeshBasicMaterial||d instanceof THREE.ShaderMaterial||d.skinning)&&null!==ka.viewMatrix&&m.uniformMatrix4fv(ka.viewMatrix,!1,a.matrixWorldInverse.elements);if(d.skinning)if(e.bindMatrix&&null!==ka.bindMatrix&&m.uniformMatrix4fv(ka.bindMatrix,!1,e.bindMatrix.elements),e.bindMatrixInverse&&null!==ka.bindMatrixInverse&&m.uniformMatrix4fv(ka.bindMatrixInverse,!1,e.bindMatrixInverse.elements),"
                << std::endl ;
            out
                << "Nb&&e.skeleton&&e.skeleton.useVertexTexture){if(null!==ka.boneTexture){var db=D();m.uniform1i(ka.boneTexture,db);B.setTexture(e.skeleton.boneTexture,db)}null!==ka.boneTextureWidth&&m.uniform1i(ka.boneTextureWidth,e.skeleton.boneTextureWidth);null!==ka.boneTextureHeight&&m.uniform1i(ka.boneTextureHeight,e.skeleton.boneTextureHeight)}else e.skeleton&&e.skeleton.boneMatrices&&null!==ka.boneGlobalMatrices&&m.uniformMatrix4fv(ka.boneGlobalMatrices,!1,e.skeleton.boneMatrices);if(oa){c&&d.fog&&(Q.fogColor.value="
                << std::endl ;
            out
                << "c.color,c instanceof THREE.Fog?(Q.fogNear.value=c.near,Q.fogFar.value=c.far):c instanceof THREE.FogExp2&&(Q.fogDensity.value=c.density));if(d instanceof THREE.MeshPhongMaterial||d instanceof THREE.MeshLambertMaterial||d.lights){if(Ob){var qa=!0,ia,Za,Y,bb=0,cb=0,ib=0,xb,pb,qb,Ca,jb,na=jc,rb=na.directional.colors,La=na.directional.positions,Ma=na.point.colors,kb=na.point.positions,yb=na.point.distances,zb=na.point.decays,Ra=na.spot.colors,Aa=na.spot.positions,eb=na.spot.distances,Ha=na.spot.directions,"
                << std::endl ;
            out
                << "la=na.spot.anglesCos,ra=na.spot.exponents,K=na.spot.decays,za=na.hemi.skyColors,Sa=na.hemi.groundColors,Ab=na.hemi.positions,lb=0,Na=0,sa=0,Da=0,Ea=0,Fa=0,Gb=0,Bb=0,mb=0,C=0,Va=0,Ta=0;ia=0;for(Za=b.length;ia<Za;ia++)Y=b[ia],Y.onlyShadow||(xb=Y.color,Ca=Y.intensity,jb=Y.distance,Y instanceof THREE.AmbientLight?Y.visible&&(bb+=xb.r,cb+=xb.g,ib+=xb.b):Y instanceof THREE.DirectionalLight?(Ea+=1,Y.visible&&(pa.setFromMatrixPosition(Y.matrixWorld),wa.setFromMatrixPosition(Y.target.matrixWorld),pa.sub(wa),"
                << std::endl ;
            out
                << "pa.normalize(),mb=3*lb,La[mb]=pa.x,La[mb+1]=pa.y,La[mb+2]=pa.z,y(rb,mb,xb,Ca),lb+=1)):Y instanceof THREE.PointLight?(Fa+=1,Y.visible&&(C=3*Na,y(Ma,C,xb,Ca),wa.setFromMatrixPosition(Y.matrixWorld),kb[C]=wa.x,kb[C+1]=wa.y,kb[C+2]=wa.z,yb[Na]=jb,zb[Na]=0===Y.distance?0:Y.decay,Na+=1)):Y instanceof THREE.SpotLight?(Gb+=1,Y.visible&&(Va=3*sa,y(Ra,Va,xb,Ca),pa.setFromMatrixPosition(Y.matrixWorld),Aa[Va]=pa.x,Aa[Va+1]=pa.y,Aa[Va+2]=pa.z,eb[sa]=jb,wa.setFromMatrixPosition(Y.target.matrixWorld),pa.sub(wa),"
                << std::endl ;
            out
                << "pa.normalize(),Ha[Va]=pa.x,Ha[Va+1]=pa.y,Ha[Va+2]=pa.z,la[sa]=Math.cos(Y.angle),ra[sa]=Y.exponent,K[sa]=0===Y.distance?0:Y.decay,sa+=1)):Y instanceof THREE.HemisphereLight&&(Bb+=1,Y.visible&&(pa.setFromMatrixPosition(Y.matrixWorld),pa.normalize(),Ta=3*Da,Ab[Ta]=pa.x,Ab[Ta+1]=pa.y,Ab[Ta+2]=pa.z,pb=Y.color,qb=Y.groundColor,y(za,Ta,pb,Ca),y(Sa,Ta,qb,Ca),Da+=1)));ia=3*lb;for(Za=Math.max(rb.length,3*Ea);ia<Za;ia++)rb[ia]=0;ia=3*Na;for(Za=Math.max(Ma.length,3*Fa);ia<Za;ia++)Ma[ia]=0;ia=3*sa;for(Za=Math.max(Ra.length,"
                << std::endl ;
            out
                << "3*Gb);ia<Za;ia++)Ra[ia]=0;ia=3*Da;for(Za=Math.max(za.length,3*Bb);ia<Za;ia++)za[ia]=0;ia=3*Da;for(Za=Math.max(Sa.length,3*Bb);ia<Za;ia++)Sa[ia]=0;na.directional.length=lb;na.point.length=Na;na.spot.length=sa;na.hemi.length=Da;na.ambient[0]=bb;na.ambient[1]=cb;na.ambient[2]=ib;Ob=!1}if(qa){var ea=jc;Q.ambientLightColor.value=ea.ambient;Q.directionalLightColor.value=ea.directional.colors;Q.directionalLightDirection.value=ea.directional.positions;Q.pointLightColor.value=ea.point.colors;Q.pointLightPosition.value="
                << std::endl ;
            out
                << "ea.point.positions;Q.pointLightDistance.value=ea.point.distances;Q.pointLightDecay.value=ea.point.decays;Q.spotLightColor.value=ea.spot.colors;Q.spotLightPosition.value=ea.spot.positions;Q.spotLightDistance.value=ea.spot.distances;Q.spotLightDirection.value=ea.spot.directions;Q.spotLightAngleCos.value=ea.spot.anglesCos;Q.spotLightExponent.value=ea.spot.exponents;Q.spotLightDecay.value=ea.spot.decays;Q.hemisphereLightSkyColor.value=ea.hemi.skyColors;Q.hemisphereLightGroundColor.value=ea.hemi.groundColors;"
                << std::endl ;
            out
                << "Q.hemisphereLightDirection.value=ea.hemi.positions;x(Q,!0)}else x(Q,!1)}if(d instanceof THREE.MeshBasicMaterial||d instanceof THREE.MeshLambertMaterial||d instanceof THREE.MeshPhongMaterial){Q.opacity.value=d.opacity;Q.diffuse.value=d.color;Q.map.value=d.map;Q.lightMap.value=d.lightMap;Q.specularMap.value=d.specularMap;Q.alphaMap.value=d.alphaMap;d.bumpMap&&(Q.bumpMap.value=d.bumpMap,Q.bumpScale.value=d.bumpScale);d.normalMap&&(Q.normalMap.value=d.normalMap,Q.normalScale.value.copy(d.normalScale));"
                << std::endl ;
            out
                << "var fb;d.map?fb=d.map:d.specularMap?fb=d.specularMap:d.normalMap?fb=d.normalMap:d.bumpMap?fb=d.bumpMap:d.alphaMap&&(fb=d.alphaMap);if(void 0!==fb){var wb=fb.offset,Qb=fb.repeat;Q.offsetRepeat.value.set(wb.x,wb.y,Qb.x,Qb.y)}Q.envMap.value=d.envMap;Q.flipEnvMap.value=d.envMap instanceof THREE.WebGLRenderTargetCube?1:-1;Q.reflectivity.value=d.reflectivity;Q.refractionRatio.value=d.refractionRatio}if(d instanceof THREE.LineBasicMaterial)Q.diffuse.value=d.color,Q.opacity.value=d.opacity;else if(d instanceof"
                << std::endl ;
            out
                << "THREE.LineDashedMaterial)Q.diffuse.value=d.color,Q.opacity.value=d.opacity,Q.dashSize.value=d.dashSize,Q.totalSize.value=d.dashSize+d.gapSize,Q.scale.value=d.scale;else if(d instanceof THREE.PointCloudMaterial){if(Q.psColor.value=d.color,Q.opacity.value=d.opacity,Q.size.value=d.size,Q.scale.value=U.height/2,Q.map.value=d.map,null!==d.map){var Wb=d.map.offset,Xb=d.map.repeat;Q.offsetRepeat.value.set(Wb.x,Wb.y,Xb.x,Xb.y)}}else d instanceof THREE.MeshPhongMaterial?(Q.shininess.value=d.shininess,Q.emissive.value="
                << std::endl ;
            out
                << "d.emissive,Q.specular.value=d.specular,d.wrapAround&&Q.wrapRGB.value.copy(d.wrapRGB)):d instanceof THREE.MeshLambertMaterial?(Q.emissive.value=d.emissive,d.wrapAround&&Q.wrapRGB.value.copy(d.wrapRGB)):d instanceof THREE.MeshDepthMaterial?(Q.mNear.value=a.near,Q.mFar.value=a.far,Q.opacity.value=d.opacity):d instanceof THREE.MeshNormalMaterial&&(Q.opacity.value=d.opacity);if(e.receiveShadow&&!d._shadowPass&&Q.shadowMatrix)for(var sb=0,Cb=0,$=b.length;Cb<$;Cb++){var xa=b[Cb];xa.castShadow&&(xa instanceof"
                << std::endl ;
            out
                << "THREE.SpotLight||xa instanceof THREE.DirectionalLight&&!xa.shadowCascade)&&(Q.shadowMap.value[sb]=xa.shadowMap,Q.shadowMapSize.value[sb]=xa.shadowMapSize,Q.shadowMatrix.value[sb]=xa.shadowMatrix,Q.shadowDarkness.value[sb]=xa.shadowDarkness,Q.shadowBias.value[sb]=xa.shadowBias,sb++)}for(var Hb=d.uniformsList,Oa,$a,Ga,nb=0,fc=Hb.length;nb<fc;nb++){var T=Hb[nb][0];if(!1!==T.needsUpdate){var Db=T.type,S=T.value,aa=Hb[nb][1];switch(Db){case \"1i\":m.uniform1i(aa,S);break;case \"1f\":m.uniform1f(aa,S);break;"
                << std::endl ;
            out
                << "case \"2f\":m.uniform2f(aa,S[0],S[1]);break;case \"3f\":m.uniform3f(aa,S[0],S[1],S[2]);break;case \"4f\":m.uniform4f(aa,S[0],S[1],S[2],S[3]);break;case \"1iv\":m.uniform1iv(aa,S);break;case \"3iv\":m.uniform3iv(aa,S);break;case \"1fv\":m.uniform1fv(aa,S);break;case \"2fv\":m.uniform2fv(aa,S);break;case \"3fv\":m.uniform3fv(aa,S);break;case \"4fv\":m.uniform4fv(aa,S);break;case \"Matrix3fv\":m.uniformMatrix3fv(aa,!1,S);break;case \"Matrix4fv\":m.uniformMatrix4fv(aa,!1,S);break;case \"i\":m.uniform1i(aa,S);break;case \"f\":m.uniform1f(aa,"
                << std::endl ;
            out
                << "S);break;case \"v2\":m.uniform2f(aa,S.x,S.y);break;case \"v3\":m.uniform3f(aa,S.x,S.y,S.z);break;case \"v4\":m.uniform4f(aa,S.x,S.y,S.z,S.w);break;case \"c\":m.uniform3f(aa,S.r,S.g,S.b);break;case \"iv1\":m.uniform1iv(aa,S);break;case \"iv\":m.uniform3iv(aa,S);break;case \"fv1\":m.uniform1fv(aa,S);break;case \"fv\":m.uniform3fv(aa,S);break;case \"v2v\":void 0===T._array&&(T._array=new Float32Array(2*S.length));for(var Z=0,Ba=S.length;Z<Ba;Z++)Ga=2*Z,T._array[Ga]=S[Z].x,T._array[Ga+1]=S[Z].y;m.uniform2fv(aa,T._array);"
                << std::endl ;
            out
                << "break;case \"v3v\":void 0===T._array&&(T._array=new Float32Array(3*S.length));Z=0;for(Ba=S.length;Z<Ba;Z++)Ga=3*Z,T._array[Ga]=S[Z].x,T._array[Ga+1]=S[Z].y,T._array[Ga+2]=S[Z].z;m.uniform3fv(aa,T._array);break;case \"v4v\":void 0===T._array&&(T._array=new Float32Array(4*S.length));Z=0;for(Ba=S.length;Z<Ba;Z++)Ga=4*Z,T._array[Ga]=S[Z].x,T._array[Ga+1]=S[Z].y,T._array[Ga+2]=S[Z].z,T._array[Ga+3]=S[Z].w;m.uniform4fv(aa,T._array);break;case \"m3\":m.uniformMatrix3fv(aa,!1,S.elements);break;case \"m3v\":void 0==="
                << std::endl ;
            out
                << "T._array&&(T._array=new Float32Array(9*S.length));Z=0;for(Ba=S.length;Z<Ba;Z++)S[Z].flattenToArrayOffset(T._array,9*Z);m.uniformMatrix3fv(aa,!1,T._array);break;case \"m4\":m.uniformMatrix4fv(aa,!1,S.elements);break;case \"m4v\":void 0===T._array&&(T._array=new Float32Array(16*S.length));Z=0;for(Ba=S.length;Z<Ba;Z++)S[Z].flattenToArrayOffset(T._array,16*Z);m.uniformMatrix4fv(aa,!1,T._array);break;case \"t\":Oa=S;$a=D();m.uniform1i(aa,$a);if(!Oa)continue;if(Oa instanceof THREE.CubeTexture||Oa.image instanceof"
                << std::endl ;
            out
                << "Array&&6===Oa.image.length){var X=Oa,Eb=$a;if(6===X.image.length)if(X.needsUpdate){X.image.__webglTextureCube||(X.addEventListener(\"dispose\",Pb),X.image.__webglTextureCube=m.createTexture(),B.info.memory.textures++);m.activeTexture(m.TEXTURE0+Eb);m.bindTexture(m.TEXTURE_CUBE_MAP,X.image.__webglTextureCube);m.pixelStorei(m.UNPACK_FLIP_Y_WEBGL,X.flipY);for(var Rb=X instanceof THREE.CompressedTexture,Ib=X.image[0]instanceof THREE.DataTexture,gb=[],ya=0;6>ya;ya++)gb[ya]=!B.autoScaleCubemaps||Rb||Ib?Ib?"
                << std::endl ;
            out
                << "X.image[ya].image:X.image[ya]:E(X.image[ya],qc);var Yb=gb[0],Zb=THREE.Math.isPowerOfTwo(Yb.width)&&THREE.Math.isPowerOfTwo(Yb.height),Wa=I(X.format),tb=I(X.type);A(m.TEXTURE_CUBE_MAP,X,Zb);for(ya=0;6>ya;ya++)if(Rb)for(var hb,$b=gb[ya].mipmaps,Fb=0,Sb=$b.length;Fb<Sb;Fb++)hb=$b[Fb],X.format!==THREE.RGBAFormat&&X.format!==THREE.RGBFormat?-1<kc().indexOf(Wa)?m.compressedTexImage2D(m.TEXTURE_CUBE_MAP_POSITIVE_X+ya,Fb,Wa,hb.width,hb.height,0,hb.data):THREE.warn(\"THREE.WebGLRenderer: Attempt to load unsupported compressed texture format in .setCubeTexture()\"):"
                << std::endl ;
            out
                << "m.texImage2D(m.TEXTURE_CUBE_MAP_POSITIVE_X+ya,Fb,Wa,hb.width,hb.height,0,Wa,tb,hb.data);else Ib?m.texImage2D(m.TEXTURE_CUBE_MAP_POSITIVE_X+ya,0,Wa,gb[ya].width,gb[ya].height,0,Wa,tb,gb[ya].data):m.texImage2D(m.TEXTURE_CUBE_MAP_POSITIVE_X+ya,0,Wa,Wa,tb,gb[ya]);X.generateMipmaps&&Zb&&m.generateMipmap(m.TEXTURE_CUBE_MAP);X.needsUpdate=!1;if(X.onUpdate)X.onUpdate()}else m.activeTexture(m.TEXTURE0+Eb),m.bindTexture(m.TEXTURE_CUBE_MAP,X.image.__webglTextureCube)}else if(Oa instanceof THREE.WebGLRenderTargetCube){var Jb="
                << std::endl ;
            out
                << "Oa;m.activeTexture(m.TEXTURE0+$a);m.bindTexture(m.TEXTURE_CUBE_MAP,Jb.__webglTexture)}else B.setTexture(Oa,$a);break;case \"tv\":void 0===T._array&&(T._array=[]);Z=0;for(Ba=T.value.length;Z<Ba;Z++)T._array[Z]=D();m.uniform1iv(aa,T._array);Z=0;for(Ba=T.value.length;Z<Ba;Z++)Oa=T.value[Z],$a=T._array[Z],Oa&&B.setTexture(Oa,$a);break;default:THREE.warn(\"THREE.WebGLRenderer: Unknown uniform type: \"+Db)}}}}m.uniformMatrix4fv(ka.modelViewMatrix,!1,e._modelViewMatrix.elements);ka.normalMatrix&&m.uniformMatrix3fv(ka.normalMatrix,"
                << std::endl ;
            out
                << "!1,e._normalMatrix.elements);null!==ka.modelMatrix&&m.uniformMatrix4fv(ka.modelMatrix,!1,e.matrixWorld.elements);return Ua}function x(a,b){a.ambientLightColor.needsUpdate=b;a.directionalLightColor.needsUpdate=b;a.directionalLightDirection.needsUpdate=b;a.pointLightColor.needsUpdate=b;a.pointLightPosition.needsUpdate=b;a.pointLightDistance.needsUpdate=b;a.pointLightDecay.needsUpdate=b;a.spotLightColor.needsUpdate=b;a.spotLightPosition.needsUpdate=b;a.spotLightDistance.needsUpdate=b;a.spotLightDirection.needsUpdate="
                << std::endl ;
            out
                << "b;a.spotLightAngleCos.needsUpdate=b;a.spotLightExponent.needsUpdate=b;a.spotLightDecay.needsUpdate=b;a.hemisphereLightSkyColor.needsUpdate=b;a.hemisphereLightGroundColor.needsUpdate=b;a.hemisphereLightDirection.needsUpdate=b}function D(){var a=Mb;a>=Wb&&THREE.warn(\"WebGLRenderer: trying to use \"+a+\" texture units while this GPU supports only \"+Wb);Mb+=1;return a}function w(a,b){a._modelViewMatrix.multiplyMatrices(b.matrixWorldInverse,a.matrixWorld);a._normalMatrix.getNormalMatrix(a._modelViewMatrix)}"
                << std::endl ;
            out
                << "function y(a,b,c,d){a[b]=c.r*d;a[b+1]=c.g*d;a[b+2]=c.b*d}function A(a,b,c){c?(m.texParameteri(a,m.TEXTURE_WRAP_S,I(b.wrapS)),m.texParameteri(a,m.TEXTURE_WRAP_T,I(b.wrapT)),m.texParameteri(a,m.TEXTURE_MAG_FILTER,I(b.magFilter)),m.texParameteri(a,m.TEXTURE_MIN_FILTER,I(b.minFilter))):(m.texParameteri(a,m.TEXTURE_WRAP_S,m.CLAMP_TO_EDGE),m.texParameteri(a,m.TEXTURE_WRAP_T,m.CLAMP_TO_EDGE),b.wrapS===THREE.ClampToEdgeWrapping&&b.wrapT===THREE.ClampToEdgeWrapping||THREE.warn(\"THREE.WebGLRenderer: Texture is not power of two. Texture.wrapS and Texture.wrapT should be set to THREE.ClampToEdgeWrapping. ( \"+"
                << std::endl ;
            out
                << "b.sourceFile+\" )\"),m.texParameteri(a,m.TEXTURE_MAG_FILTER,z(b.magFilter)),m.texParameteri(a,m.TEXTURE_MIN_FILTER,z(b.minFilter)),b.minFilter!==THREE.NearestFilter&&b.minFilter!==THREE.LinearFilter&&THREE.warn(\"THREE.WebGLRenderer: Texture is not power of two. Texture.minFilter should be set to THREE.NearestFilter or THREE.LinearFilter. ( \"+b.sourceFile+\" )\"));(c=da.get(\"EXT_texture_filter_anisotropic\"))&&b.type!==THREE.FloatType&&b.type!==THREE.HalfFloatType&&(1<b.anisotropy||b.__currentAnisotropy)&&"
                << std::endl ;
            out
                << "(m.texParameterf(a,c.TEXTURE_MAX_ANISOTROPY_EXT,Math.min(b.anisotropy,B.getMaxAnisotropy())),b.__currentAnisotropy=b.anisotropy)}function E(a,b){if(a.width>b||a.height>b){var c=b/Math.max(a.width,a.height),d=document.createElement(\"canvas\");d.width=Math.floor(a.width*c);d.height=Math.floor(a.height*c);d.getContext(\"2d\").drawImage(a,0,0,a.width,a.height,0,0,d.width,d.height);THREE.warn(\"THREE.WebGLRenderer: image is too big (\"+a.width+\"x\"+a.height+\"). Resized to \"+d.width+\"x\"+d.height,a);return d}return a}"
                << std::endl ;
            out
                << "function G(a,b){m.bindRenderbuffer(m.RENDERBUFFER,a);b.depthBuffer&&!b.stencilBuffer?(m.renderbufferStorage(m.RENDERBUFFER,m.DEPTH_COMPONENT16,b.width,b.height),m.framebufferRenderbuffer(m.FRAMEBUFFER,m.DEPTH_ATTACHMENT,m.RENDERBUFFER,a)):b.depthBuffer&&b.stencilBuffer?(m.renderbufferStorage(m.RENDERBUFFER,m.DEPTH_STENCIL,b.width,b.height),m.framebufferRenderbuffer(m.FRAMEBUFFER,m.DEPTH_STENCIL_ATTACHMENT,m.RENDERBUFFER,a)):m.renderbufferStorage(m.RENDERBUFFER,m.RGBA4,b.width,b.height)}function F(a){a instanceof"
                << std::endl ;
            out
                << "THREE.WebGLRenderTargetCube?(m.bindTexture(m.TEXTURE_CUBE_MAP,a.__webglTexture),m.generateMipmap(m.TEXTURE_CUBE_MAP),m.bindTexture(m.TEXTURE_CUBE_MAP,null)):(m.bindTexture(m.TEXTURE_2D,a.__webglTexture),m.generateMipmap(m.TEXTURE_2D),m.bindTexture(m.TEXTURE_2D,null))}function z(a){return a===THREE.NearestFilter||a===THREE.NearestMipMapNearestFilter||a===THREE.NearestMipMapLinearFilter?m.NEAREST:m.LINEAR}function I(a){var b;if(a===THREE.RepeatWrapping)return m.REPEAT;if(a===THREE.ClampToEdgeWrapping)return m.CLAMP_TO_EDGE;"
                << std::endl ;
            out
                << "if(a===THREE.MirroredRepeatWrapping)return m.MIRRORED_REPEAT;if(a===THREE.NearestFilter)return m.NEAREST;if(a===THREE.NearestMipMapNearestFilter)return m.NEAREST_MIPMAP_NEAREST;if(a===THREE.NearestMipMapLinearFilter)return m.NEAREST_MIPMAP_LINEAR;if(a===THREE.LinearFilter)return m.LINEAR;if(a===THREE.LinearMipMapNearestFilter)return m.LINEAR_MIPMAP_NEAREST;if(a===THREE.LinearMipMapLinearFilter)return m.LINEAR_MIPMAP_LINEAR;if(a===THREE.UnsignedByteType)return m.UNSIGNED_BYTE;if(a===THREE.UnsignedShort4444Type)return m.UNSIGNED_SHORT_4_4_4_4;"
                << std::endl ;
            out
                << "if(a===THREE.UnsignedShort5551Type)return m.UNSIGNED_SHORT_5_5_5_1;if(a===THREE.UnsignedShort565Type)return m.UNSIGNED_SHORT_5_6_5;if(a===THREE.ByteType)return m.BYTE;if(a===THREE.ShortType)return m.SHORT;if(a===THREE.UnsignedShortType)return m.UNSIGNED_SHORT;if(a===THREE.IntType)return m.INT;if(a===THREE.UnsignedIntType)return m.UNSIGNED_INT;if(a===THREE.FloatType)return m.FLOAT;b=da.get(\"OES_texture_half_float\");if(null!==b&&a===THREE.HalfFloatType)return b.HALF_FLOAT_OES;if(a===THREE.AlphaFormat)return m.ALPHA;"
                << std::endl ;
            out
                << "if(a===THREE.RGBFormat)return m.RGB;if(a===THREE.RGBAFormat)return m.RGBA;if(a===THREE.LuminanceFormat)return m.LUMINANCE;if(a===THREE.LuminanceAlphaFormat)return m.LUMINANCE_ALPHA;if(a===THREE.AddEquation)return m.FUNC_ADD;if(a===THREE.SubtractEquation)return m.FUNC_SUBTRACT;if(a===THREE.ReverseSubtractEquation)return m.FUNC_REVERSE_SUBTRACT;if(a===THREE.ZeroFactor)return m.ZERO;if(a===THREE.OneFactor)return m.ONE;if(a===THREE.SrcColorFactor)return m.SRC_COLOR;if(a===THREE.OneMinusSrcColorFactor)return m.ONE_MINUS_SRC_COLOR;"
                << std::endl ;
            out
                << "if(a===THREE.SrcAlphaFactor)return m.SRC_ALPHA;if(a===THREE.OneMinusSrcAlphaFactor)return m.ONE_MINUS_SRC_ALPHA;if(a===THREE.DstAlphaFactor)return m.DST_ALPHA;if(a===THREE.OneMinusDstAlphaFactor)return m.ONE_MINUS_DST_ALPHA;if(a===THREE.DstColorFactor)return m.DST_COLOR;if(a===THREE.OneMinusDstColorFactor)return m.ONE_MINUS_DST_COLOR;if(a===THREE.SrcAlphaSaturateFactor)return m.SRC_ALPHA_SATURATE;b=da.get(\"WEBGL_compressed_texture_s3tc\");if(null!==b){if(a===THREE.RGB_S3TC_DXT1_Format)return b.COMPRESSED_RGB_S3TC_DXT1_EXT;"
                << std::endl ;
            out
                << "if(a===THREE.RGBA_S3TC_DXT1_Format)return b.COMPRESSED_RGBA_S3TC_DXT1_EXT;if(a===THREE.RGBA_S3TC_DXT3_Format)return b.COMPRESSED_RGBA_S3TC_DXT3_EXT;if(a===THREE.RGBA_S3TC_DXT5_Format)return b.COMPRESSED_RGBA_S3TC_DXT5_EXT}b=da.get(\"WEBGL_compressed_texture_pvrtc\");if(null!==b){if(a===THREE.RGB_PVRTC_4BPPV1_Format)return b.COMPRESSED_RGB_PVRTC_4BPPV1_IMG;if(a===THREE.RGB_PVRTC_2BPPV1_Format)return b.COMPRESSED_RGB_PVRTC_2BPPV1_IMG;if(a===THREE.RGBA_PVRTC_4BPPV1_Format)return b.COMPRESSED_RGBA_PVRTC_4BPPV1_IMG;"
                << std::endl ;
            out
                << "if(a===THREE.RGBA_PVRTC_2BPPV1_Format)return b.COMPRESSED_RGBA_PVRTC_2BPPV1_IMG}b=da.get(\"EXT_blend_minmax\");if(null!==b){if(a===THREE.MinEquation)return b.MIN_EXT;if(a===THREE.MaxEquation)return b.MAX_EXT}return 0}console.log(\"THREE.WebGLRenderer\",THREE.REVISION);a=a||{};var U=void 0!==a.canvas?a.canvas:document.createElement(\"canvas\"),M=void 0!==a.context?a.context:null,H=1,L=void 0!==a.precision?a.precision:\"highp\",P=void 0!==a.alpha?a.alpha:!1,N=void 0!==a.depth?a.depth:!0,R=void 0!==a.stencil?"
                << std::endl ;
            out
                << "a.stencil:!0,V=void 0!==a.antialias?a.antialias:!1,J=void 0!==a.premultipliedAlpha?a.premultipliedAlpha:!0,oa=void 0!==a.preserveDrawingBuffer?a.preserveDrawingBuffer:!1,ja=void 0!==a.logarithmicDepthBuffer?a.logarithmicDepthBuffer:!1,ha=new THREE.Color(0),O=0,ca=[],ba={},qa=[],Ka=[],Qa=[],Xa=[],Ya=[];this.domElement=U;this.context=null;this.sortObjects=this.autoClearStencil=this.autoClearDepth=this.autoClearColor=this.autoClear=!0;this.gammaFactor=2;this.shadowMapEnabled=this.gammaOutput=this.gammaInput="
                << std::endl ;
            out
                << "!1;this.shadowMapType=THREE.PCFShadowMap;this.shadowMapCullFace=THREE.CullFaceFront;this.shadowMapCascade=this.shadowMapDebug=!1;this.maxMorphTargets=8;this.maxMorphNormals=4;this.autoScaleCubemaps=!0;this.info={memory:{programs:0,geometries:0,textures:0},render:{calls:0,vertices:0,faces:0,points:0}};var B=this,Pa=[],ob=null,ab=null,ub=-1,ta=\"\",vb=null,Mb=0,ib=0,bb=0,pb=U.width,qb=U.height,Xb=0,fc=0,cb=new THREE.Frustum,db=new THREE.Matrix4,wa=new THREE.Vector3,pa=new THREE.Vector3,Ob=!0,jc={ambient:[0,"
                << std::endl ;
            out
                << "0,0],directional:{length:0,colors:[],positions:[]},point:{length:0,colors:[],positions:[],distances:[],decays:[]},spot:{length:0,colors:[],positions:[],distances:[],directions:[],anglesCos:[],exponents:[],decays:[]},hemi:{length:0,skyColors:[],groundColors:[],positions:[]}},m;try{var Yb={alpha:P,depth:N,stencil:R,antialias:V,premultipliedAlpha:J,preserveDrawingBuffer:oa};m=M||U.getContext(\"webgl\",Yb)||U.getContext(\"experimental-webgl\",Yb);if(null===m){if(null!==U.getContext(\"webgl\"))throw\"Error creating WebGL context with your selected attributes.\";"
                << std::endl ;
            out
                << "throw\"Error creating WebGL context.\";}U.addEventListener(\"webglcontextlost\",function(a){a.preventDefault();Zb();lc();ba={}},!1)}catch(rc){THREE.error(\"THREE.WebGLRenderer: \"+rc)}var W=new THREE.WebGLState(m,I);void 0===m.getShaderPrecisionFormat&&(m.getShaderPrecisionFormat=function(){return{rangeMin:1,rangeMax:1,precision:1}});var da=new THREE.WebGLExtensions(m);da.get(\"OES_texture_float\");da.get(\"OES_texture_float_linear\");da.get(\"OES_texture_half_float\");da.get(\"OES_texture_half_float_linear\");"
                << std::endl ;
            out
                << "da.get(\"OES_standard_derivatives\");ja&&da.get(\"EXT_frag_depth\");var rb=function(a,b,c,d){!0===J&&(a*=d,b*=d,c*=d);m.clearColor(a,b,c,d)},lc=function(){m.clearColor(0,0,0,1);m.clearDepth(1);m.clearStencil(0);m.enable(m.DEPTH_TEST);m.depthFunc(m.LEQUAL);m.frontFace(m.CCW);m.cullFace(m.BACK);m.enable(m.CULL_FACE);m.enable(m.BLEND);m.blendEquation(m.FUNC_ADD);m.blendFunc(m.SRC_ALPHA,m.ONE_MINUS_SRC_ALPHA);m.viewport(ib,bb,pb,qb);rb(ha.r,ha.g,ha.b,O)},Zb=function(){vb=ob=null;ta=\"\";ub=-1;Ob=!0;W.reset()};"
                << std::endl ;
            out
                << "lc();this.context=m;this.state=W;var Wb=m.getParameter(m.MAX_TEXTURE_IMAGE_UNITS),sc=m.getParameter(m.MAX_VERTEX_TEXTURE_IMAGE_UNITS),tc=m.getParameter(m.MAX_TEXTURE_SIZE),qc=m.getParameter(m.MAX_CUBE_MAP_TEXTURE_SIZE),Vb=0<sc,Nb=Vb&&da.get(\"OES_texture_float\"),uc=m.getShaderPrecisionFormat(m.VERTEX_SHADER,m.HIGH_FLOAT),vc=m.getShaderPrecisionFormat(m.VERTEX_SHADER,m.MEDIUM_FLOAT),wc=m.getShaderPrecisionFormat(m.FRAGMENT_SHADER,m.HIGH_FLOAT),xc=m.getShaderPrecisionFormat(m.FRAGMENT_SHADER,m.MEDIUM_FLOAT),"
                << std::endl ;
            out
                << "kc=function(){var a;return function(){if(void 0!==a)return a;a=[];if(da.get(\"WEBGL_compressed_texture_pvrtc\")||da.get(\"WEBGL_compressed_texture_s3tc\"))for(var b=m.getParameter(m.COMPRESSED_TEXTURE_FORMATS),c=0;c<b.length;c++)a.push(b[c]);return a}}(),yc=0<uc.precision&&0<wc.precision,mc=0<vc.precision&&0<xc.precision;\"highp\"!==L||yc||(mc?(L=\"mediump\",THREE.warn(\"THREE.WebGLRenderer: highp not supported, using mediump.\")):(L=\"lowp\",THREE.warn(\"THREE.WebGLRenderer: highp and mediump not supported, using lowp.\")));"
                << std::endl ;
            out
                << "\"mediump\"!==L||mc||(L=\"lowp\",THREE.warn(\"THREE.WebGLRenderer: mediump not supported, using lowp.\"));var zc=new THREE.ShadowMapPlugin(this,ca,ba,qa),Ac=new THREE.SpritePlugin(this,Xa),Bc=new THREE.LensFlarePlugin(this,Ya);this.getContext=function(){return m};this.forceContextLoss=function(){da.get(\"WEBGL_lose_context\").loseContext()};this.supportsVertexTextures=function(){return Vb};this.supportsFloatTextures=function(){return da.get(\"OES_texture_float\")};this.supportsHalfFloatTextures=function(){return da.get(\"OES_texture_half_float\")};"
                << std::endl ;
            out
                << "this.supportsStandardDerivatives=function(){return da.get(\"OES_standard_derivatives\")};this.supportsCompressedTextureS3TC=function(){return da.get(\"WEBGL_compressed_texture_s3tc\")};this.supportsCompressedTexturePVRTC=function(){return da.get(\"WEBGL_compressed_texture_pvrtc\")};this.supportsBlendMinMax=function(){return da.get(\"EXT_blend_minmax\")};this.getMaxAnisotropy=function(){var a;return function(){if(void 0!==a)return a;var b=da.get(\"EXT_texture_filter_anisotropic\");return a=null!==b?m.getParameter(b.MAX_TEXTURE_MAX_ANISOTROPY_EXT):"
                << std::endl ;
            out
                << "0}}();this.getPrecision=function(){return L};this.getPixelRatio=function(){return H};this.setPixelRatio=function(a){H=a};this.setSize=function(a,b,c){U.width=a*H;U.height=b*H;!1!==c&&(U.style.width=a+\"px\",U.style.height=b+\"px\");this.setViewport(0,0,a,b)};this.setViewport=function(a,b,c,d){ib=a*H;bb=b*H;pb=c*H;qb=d*H;m.viewport(ib,bb,pb,qb)};this.setScissor=function(a,b,c,d){m.scissor(a*H,b*H,c*H,d*H)};this.enableScissorTest=function(a){a?m.enable(m.SCISSOR_TEST):m.disable(m.SCISSOR_TEST)};this.getClearColor="
                << std::endl ;
            out
                << "function(){return ha};this.setClearColor=function(a,b){ha.set(a);O=void 0!==b?b:1;rb(ha.r,ha.g,ha.b,O)};this.getClearAlpha=function(){return O};this.setClearAlpha=function(a){O=a;rb(ha.r,ha.g,ha.b,O)};this.clear=function(a,b,c){var d=0;if(void 0===a||a)d|=m.COLOR_BUFFER_BIT;if(void 0===b||b)d|=m.DEPTH_BUFFER_BIT;if(void 0===c||c)d|=m.STENCIL_BUFFER_BIT;m.clear(d)};this.clearColor=function(){m.clear(m.COLOR_BUFFER_BIT)};this.clearDepth=function(){m.clear(m.DEPTH_BUFFER_BIT)};this.clearStencil=function(){m.clear(m.STENCIL_BUFFER_BIT)};"
                << std::endl ;
            out
                << "this.clearTarget=function(a,b,c,d){this.setRenderTarget(a);this.clear(b,c,d)};this.resetGLState=Zb;var wb=function(a){a.target.traverse(function(a){a.removeEventListener(\"remove\",wb);if(a instanceof THREE.Mesh||a instanceof THREE.PointCloud||a instanceof THREE.Line)delete ba[a.id];else if(a instanceof THREE.ImmediateRenderObject||a.immediateRenderCallback)for(var b=qa,c=b.length-1;0<=c;c--)b[c].object===a&&b.splice(c,1);delete a.__webglInit;delete a._modelViewMatrix;delete a._normalMatrix;delete a.__webglActive})},"
                << std::endl ;
            out
                << "jb=function(a){a=a.target;a.removeEventListener(\"dispose\",jb);delete a.__webglInit;if(a instanceof THREE.BufferGeometry){for(var b in a.attributes){var c=a.attributes[b];void 0!==c.buffer&&(m.deleteBuffer(c.buffer),delete c.buffer)}B.info.memory.geometries--}else if(b=Ua[a.id],void 0!==b){for(var c=0,d=b.length;c<d;c++){var e=b[c];if(void 0!==e.numMorphTargets){for(var f=0,g=e.numMorphTargets;f<g;f++)m.deleteBuffer(e.__webglMorphTargetsBuffers[f]);delete e.__webglMorphTargetsBuffers}if(void 0!==e.numMorphNormals){f="
                << std::endl ;
            out
                << "0;for(g=e.numMorphNormals;f<g;f++)m.deleteBuffer(e.__webglMorphNormalsBuffers[f]);delete e.__webglMorphNormalsBuffers}nc(e)}delete Ua[a.id]}else nc(a);ta=\"\"},Pb=function(a){a=a.target;a.removeEventListener(\"dispose\",Pb);a.image&&a.image.__webglTextureCube?(m.deleteTexture(a.image.__webglTextureCube),delete a.image.__webglTextureCube):void 0!==a.__webglInit&&(m.deleteTexture(a.__webglTexture),delete a.__webglTexture,delete a.__webglInit);B.info.memory.textures--},oc=function(a){a=a.target;a.removeEventListener(\"dispose\","
                << std::endl ;
            out
                << "oc);if(a&&void 0!==a.__webglTexture){m.deleteTexture(a.__webglTexture);delete a.__webglTexture;if(a instanceof THREE.WebGLRenderTargetCube)for(var b=0;6>b;b++)m.deleteFramebuffer(a.__webglFramebuffer[b]),m.deleteRenderbuffer(a.__webglRenderbuffer[b]);else m.deleteFramebuffer(a.__webglFramebuffer),m.deleteRenderbuffer(a.__webglRenderbuffer);delete a.__webglFramebuffer;delete a.__webglRenderbuffer}B.info.memory.textures--},ic=function(a){a=a.target;a.removeEventListener(\"dispose\",ic);hc(a)},nc=function(a){for(var b="
                << std::endl ;
            out
                << "\"__webglVertexBuffer __webglNormalBuffer __webglTangentBuffer __webglColorBuffer __webglUVBuffer __webglUV2Buffer __webglSkinIndicesBuffer __webglSkinWeightsBuffer __webglFaceBuffer __webglLineBuffer __webglLineDistanceBuffer\".split(\" \"),c=0,d=b.length;c<d;c++){var e=b[c];void 0!==a[e]&&(m.deleteBuffer(a[e]),delete a[e])}if(void 0!==a.__webglCustomAttributesList){for(e in a.__webglCustomAttributesList)m.deleteBuffer(a.__webglCustomAttributesList[e].buffer);delete a.__webglCustomAttributesList}B.info.memory.geometries--},"
                << std::endl ;
            out
                << "hc=function(a){var b=a.program.program;if(void 0!==b){a.program=void 0;var c,d,e=!1;a=0;for(c=Pa.length;a<c;a++)if(d=Pa[a],d.program===b){d.usedTimes--;0===d.usedTimes&&(e=!0);break}if(!0===e){e=[];a=0;for(c=Pa.length;a<c;a++)d=Pa[a],d.program!==b&&e.push(d);Pa=e;m.deleteProgram(b);B.info.memory.programs--}}};this.renderBufferImmediate=function(a,b,c){W.initAttributes();a.hasPositions&&!a.__webglVertexBuffer&&(a.__webglVertexBuffer=m.createBuffer());a.hasNormals&&!a.__webglNormalBuffer&&(a.__webglNormalBuffer="
                << std::endl ;
            out
                << "m.createBuffer());a.hasUvs&&!a.__webglUvBuffer&&(a.__webglUvBuffer=m.createBuffer());a.hasColors&&!a.__webglColorBuffer&&(a.__webglColorBuffer=m.createBuffer());a.hasPositions&&(m.bindBuffer(m.ARRAY_BUFFER,a.__webglVertexBuffer),m.bufferData(m.ARRAY_BUFFER,a.positionArray,m.DYNAMIC_DRAW),W.enableAttribute(b.attributes.position),m.vertexAttribPointer(b.attributes.position,3,m.FLOAT,!1,0,0));if(a.hasNormals){m.bindBuffer(m.ARRAY_BUFFER,a.__webglNormalBuffer);if(!1===c instanceof THREE.MeshPhongMaterial&&"
                << std::endl ;
            out
                << "c.shading===THREE.FlatShading){var d,e,f,g,h,k,n,l,p,q,r,s=3*a.count;for(r=0;r<s;r+=9)q=a.normalArray,d=q[r],e=q[r+1],f=q[r+2],g=q[r+3],k=q[r+4],l=q[r+5],h=q[r+6],n=q[r+7],p=q[r+8],d=(d+g+h)/3,e=(e+k+n)/3,f=(f+l+p)/3,q[r]=d,q[r+1]=e,q[r+2]=f,q[r+3]=d,q[r+4]=e,q[r+5]=f,q[r+6]=d,q[r+7]=e,q[r+8]=f}m.bufferData(m.ARRAY_BUFFER,a.normalArray,m.DYNAMIC_DRAW);W.enableAttribute(b.attributes.normal);m.vertexAttribPointer(b.attributes.normal,3,m.FLOAT,!1,0,0)}a.hasUvs&&c.map&&(m.bindBuffer(m.ARRAY_BUFFER,a.__webglUvBuffer),"
                << std::endl ;
            out
                << "m.bufferData(m.ARRAY_BUFFER,a.uvArray,m.DYNAMIC_DRAW),W.enableAttribute(b.attributes.uv),m.vertexAttribPointer(b.attributes.uv,2,m.FLOAT,!1,0,0));a.hasColors&&c.vertexColors!==THREE.NoColors&&(m.bindBuffer(m.ARRAY_BUFFER,a.__webglColorBuffer),m.bufferData(m.ARRAY_BUFFER,a.colorArray,m.DYNAMIC_DRAW),W.enableAttribute(b.attributes.color),m.vertexAttribPointer(b.attributes.color,3,m.FLOAT,!1,0,0));W.disableUnusedAttributes();m.drawArrays(m.TRIANGLES,0,a.count);a.count=0};this.renderBufferDirect=function(a,"
                << std::endl ;
            out
                << "b,c,e,f,g){if(!1!==e.visible)if(t(g),a=v(a,b,c,e,g),b=!1,c=\"direct_\"+f.id+\"_\"+a.id+\"_\"+(e.wireframe?1:0),c!==ta&&(ta=c,b=!0),b&&W.initAttributes(),g instanceof THREE.Mesh){g=!0===e.wireframe?m.LINES:m.TRIANGLES;var h=f.attributes.index;if(h){var k,n;h.array instanceof Uint32Array&&da.get(\"OES_element_index_uint\")?(k=m.UNSIGNED_INT,n=4):(k=m.UNSIGNED_SHORT,n=2);c=f.offsets;if(0===c.length)b&&(d(e,a,f,0),m.bindBuffer(m.ELEMENT_ARRAY_BUFFER,h.buffer)),m.drawElements(g,h.array.length,k,0),B.info.render.calls++,"
                << std::endl ;
            out
                << "B.info.render.vertices+=h.array.length,B.info.render.faces+=h.array.length/3;else{b=!0;for(var l=0,p=c.length;l<p;l++){var q=c[l].index;b&&(d(e,a,f,q),m.bindBuffer(m.ELEMENT_ARRAY_BUFFER,h.buffer));m.drawElements(g,c[l].count,k,c[l].start*n);B.info.render.calls++;B.info.render.vertices+=c[l].count;B.info.render.faces+=c[l].count/3}}}else b&&d(e,a,f,0),e=f.attributes.position,m.drawArrays(g,0,e.array.length/e.itemSize),B.info.render.calls++,B.info.render.vertices+=e.array.length/e.itemSize,B.info.render.faces+="
                << std::endl ;
            out
                << "e.array.length/(3*e.itemSize)}else if(g instanceof THREE.PointCloud)if(g=m.POINTS,h=f.attributes.index)if(h.array instanceof Uint32Array&&da.get(\"OES_element_index_uint\")?(k=m.UNSIGNED_INT,n=4):(k=m.UNSIGNED_SHORT,n=2),c=f.offsets,0===c.length)b&&(d(e,a,f,0),m.bindBuffer(m.ELEMENT_ARRAY_BUFFER,h.buffer)),m.drawElements(g,h.array.length,k,0),B.info.render.calls++,B.info.render.points+=h.array.length;else for(1<c.length&&(b=!0),l=0,p=c.length;l<p;l++)q=c[l].index,b&&(d(e,a,f,q),m.bindBuffer(m.ELEMENT_ARRAY_BUFFER,"
                << std::endl ;
            out
                << "h.buffer)),m.drawElements(g,c[l].count,k,c[l].start*n),B.info.render.calls++,B.info.render.points+=c[l].count;else if(b&&d(e,a,f,0),e=f.attributes.position,c=f.offsets,0===c.length)m.drawArrays(g,0,e.array.length/3),B.info.render.calls++,B.info.render.points+=e.array.length/3;else for(l=0,p=c.length;l<p;l++)m.drawArrays(g,c[l].index,c[l].count),B.info.render.calls++,B.info.render.points+=c[l].count;else if(g instanceof THREE.Line)if(g=g.mode===THREE.LineStrip?m.LINE_STRIP:m.LINES,W.setLineWidth(e.linewidth*"
                << std::endl ;
            out
                << "H),h=f.attributes.index)if(h.array instanceof Uint32Array?(k=m.UNSIGNED_INT,n=4):(k=m.UNSIGNED_SHORT,n=2),c=f.offsets,0===c.length)b&&(d(e,a,f,0),m.bindBuffer(m.ELEMENT_ARRAY_BUFFER,h.buffer)),m.drawElements(g,h.array.length,k,0),B.info.render.calls++,B.info.render.vertices+=h.array.length;else for(1<c.length&&(b=!0),l=0,p=c.length;l<p;l++)q=c[l].index,b&&(d(e,a,f,q),m.bindBuffer(m.ELEMENT_ARRAY_BUFFER,h.buffer)),m.drawElements(g,c[l].count,k,c[l].start*n),B.info.render.calls++,B.info.render.vertices+="
                << std::endl ;
            out
                << "c[l].count;else if(b&&d(e,a,f,0),e=f.attributes.position,c=f.offsets,0===c.length)m.drawArrays(g,0,e.array.length/3),B.info.render.calls++,B.info.render.vertices+=e.array.length/3;else for(l=0,p=c.length;l<p;l++)m.drawArrays(g,c[l].index,c[l].count),B.info.render.calls++,B.info.render.vertices+=c[l].count};this.renderBuffer=function(a,b,c,d,e,f){if(!1!==d.visible){t(f);c=v(a,b,c,d,f);b=c.attributes;a=!1;c=e.id+\"_\"+c.id+\"_\"+(d.wireframe?1:0);c!==ta&&(ta=c,a=!0);a&&W.initAttributes();if(!d.morphTargets&&"
                << std::endl ;
            out
                << "0<=b.position)a&&(m.bindBuffer(m.ARRAY_BUFFER,e.__webglVertexBuffer),W.enableAttribute(b.position),m.vertexAttribPointer(b.position,3,m.FLOAT,!1,0,0));else if(f.morphTargetBase){c=d.program.attributes;-1!==f.morphTargetBase&&0<=c.position?(m.bindBuffer(m.ARRAY_BUFFER,e.__webglMorphTargetsBuffers[f.morphTargetBase]),W.enableAttribute(c.position),m.vertexAttribPointer(c.position,3,m.FLOAT,!1,0,0)):0<=c.position&&(m.bindBuffer(m.ARRAY_BUFFER,e.__webglVertexBuffer),W.enableAttribute(c.position),m.vertexAttribPointer(c.position,"
                << std::endl ;
            out
                << "3,m.FLOAT,!1,0,0));if(f.morphTargetForcedOrder.length)for(var h=0,k=f.morphTargetForcedOrder,n=f.morphTargetInfluences,l;h<d.numSupportedMorphTargets&&h<k.length;)l=c[\"morphTarget\"+h],0<=l&&(m.bindBuffer(m.ARRAY_BUFFER,e.__webglMorphTargetsBuffers[k[h]]),W.enableAttribute(l),m.vertexAttribPointer(l,3,m.FLOAT,!1,0,0)),l=c[\"morphNormal\"+h],0<=l&&d.morphNormals&&(m.bindBuffer(m.ARRAY_BUFFER,e.__webglMorphNormalsBuffers[k[h]]),W.enableAttribute(l),m.vertexAttribPointer(l,3,m.FLOAT,!1,0,0)),f.__webglMorphTargetInfluences[h]="
                << std::endl ;
            out
                << "n[k[h]],h++;else{k=[];n=f.morphTargetInfluences;h=f.geometry.morphTargets;n.length>h.length&&(console.warn(\"THREE.WebGLRenderer: Influences array is bigger than morphTargets array.\"),n.length=h.length);h=0;for(l=n.length;h<l;h++)k.push([n[h],h]);k.length>d.numSupportedMorphTargets?(k.sort(g),k.length=d.numSupportedMorphTargets):k.length>d.numSupportedMorphNormals?k.sort(g):0===k.length&&k.push([0,0]);for(var h=0,p=d.numSupportedMorphTargets;h<p;h++)if(k[h]){var q=k[h][1];l=c[\"morphTarget\"+h];0<=l&&"
                << std::endl ;
            out
                << "(m.bindBuffer(m.ARRAY_BUFFER,e.__webglMorphTargetsBuffers[q]),W.enableAttribute(l),m.vertexAttribPointer(l,3,m.FLOAT,!1,0,0));l=c[\"morphNormal\"+h];0<=l&&d.morphNormals&&(m.bindBuffer(m.ARRAY_BUFFER,e.__webglMorphNormalsBuffers[q]),W.enableAttribute(l),m.vertexAttribPointer(l,3,m.FLOAT,!1,0,0));f.__webglMorphTargetInfluences[h]=n[q]}else f.__webglMorphTargetInfluences[h]=0}null!==d.program.uniforms.morphTargetInfluences&&m.uniform1fv(d.program.uniforms.morphTargetInfluences,f.__webglMorphTargetInfluences)}if(a){if(e.__webglCustomAttributesList)for(c="
                << std::endl ;
            out
                << "0,n=e.__webglCustomAttributesList.length;c<n;c++)k=e.__webglCustomAttributesList[c],0<=b[k.buffer.belongsToAttribute]&&(m.bindBuffer(m.ARRAY_BUFFER,k.buffer),W.enableAttribute(b[k.buffer.belongsToAttribute]),m.vertexAttribPointer(b[k.buffer.belongsToAttribute],k.size,m.FLOAT,!1,0,0));0<=b.color&&(0<f.geometry.colors.length||0<f.geometry.faces.length?(m.bindBuffer(m.ARRAY_BUFFER,e.__webglColorBuffer),W.enableAttribute(b.color),m.vertexAttribPointer(b.color,3,m.FLOAT,!1,0,0)):void 0!==d.defaultAttributeValues&&"
                << std::endl ;
            out
                << "m.vertexAttrib3fv(b.color,d.defaultAttributeValues.color));0<=b.normal&&(m.bindBuffer(m.ARRAY_BUFFER,e.__webglNormalBuffer),W.enableAttribute(b.normal),m.vertexAttribPointer(b.normal,3,m.FLOAT,!1,0,0));0<=b.tangent&&(m.bindBuffer(m.ARRAY_BUFFER,e.__webglTangentBuffer),W.enableAttribute(b.tangent),m.vertexAttribPointer(b.tangent,4,m.FLOAT,!1,0,0));0<=b.uv&&(f.geometry.faceVertexUvs[0]?(m.bindBuffer(m.ARRAY_BUFFER,e.__webglUVBuffer),W.enableAttribute(b.uv),m.vertexAttribPointer(b.uv,2,m.FLOAT,!1,0,"
                << std::endl ;
            out
                << "0)):void 0!==d.defaultAttributeValues&&m.vertexAttrib2fv(b.uv,d.defaultAttributeValues.uv));0<=b.uv2&&(f.geometry.faceVertexUvs[1]?(m.bindBuffer(m.ARRAY_BUFFER,e.__webglUV2Buffer),W.enableAttribute(b.uv2),m.vertexAttribPointer(b.uv2,2,m.FLOAT,!1,0,0)):void 0!==d.defaultAttributeValues&&m.vertexAttrib2fv(b.uv2,d.defaultAttributeValues.uv2));d.skinning&&0<=b.skinIndex&&0<=b.skinWeight&&(m.bindBuffer(m.ARRAY_BUFFER,e.__webglSkinIndicesBuffer),W.enableAttribute(b.skinIndex),m.vertexAttribPointer(b.skinIndex,"
                << std::endl ;
            out
                << "4,m.FLOAT,!1,0,0),m.bindBuffer(m.ARRAY_BUFFER,e.__webglSkinWeightsBuffer),W.enableAttribute(b.skinWeight),m.vertexAttribPointer(b.skinWeight,4,m.FLOAT,!1,0,0));0<=b.lineDistance&&(m.bindBuffer(m.ARRAY_BUFFER,e.__webglLineDistanceBuffer),W.enableAttribute(b.lineDistance),m.vertexAttribPointer(b.lineDistance,1,m.FLOAT,!1,0,0))}W.disableUnusedAttributes();f instanceof THREE.Mesh?(f=e.__typeArray===Uint32Array?m.UNSIGNED_INT:m.UNSIGNED_SHORT,d.wireframe?(W.setLineWidth(d.wireframeLinewidth*H),a&&m.bindBuffer(m.ELEMENT_ARRAY_BUFFER,"
                << std::endl ;
            out
                << "e.__webglLineBuffer),m.drawElements(m.LINES,e.__webglLineCount,f,0)):(a&&m.bindBuffer(m.ELEMENT_ARRAY_BUFFER,e.__webglFaceBuffer),m.drawElements(m.TRIANGLES,e.__webglFaceCount,f,0)),B.info.render.calls++,B.info.render.vertices+=e.__webglFaceCount,B.info.render.faces+=e.__webglFaceCount/3):f instanceof THREE.Line?(f=f.mode===THREE.LineStrip?m.LINE_STRIP:m.LINES,W.setLineWidth(d.linewidth*H),m.drawArrays(f,0,e.__webglLineCount),B.info.render.calls++):f instanceof THREE.PointCloud&&(m.drawArrays(m.POINTS,"
                << std::endl ;
            out
                << "0,e.__webglParticleCount),B.info.render.calls++,B.info.render.points+=e.__webglParticleCount)}};this.render=function(a,b,c,d){if(!1===b instanceof THREE.Camera)THREE.error(\"THREE.WebGLRenderer.render: camera is not an instance of THREE.Camera.\");else{var g=a.fog;ta=\"\";ub=-1;vb=null;Ob=!0;!0===a.autoUpdate&&a.updateMatrixWorld();void 0===b.parent&&b.updateMatrixWorld();a.traverse(function(a){a instanceof THREE.SkinnedMesh&&a.skeleton.update()});b.matrixWorldInverse.getInverse(b.matrixWorld);db.multiplyMatrices(b.projectionMatrix,"
                << std::endl ;
            out
                << "b.matrixWorldInverse);cb.setFromMatrix(db);ca.length=0;Ka.length=0;Qa.length=0;Xa.length=0;Ya.length=0;h(a);!0===B.sortObjects&&(Ka.sort(e),Qa.sort(f));zc.render(a,b);B.info.render.calls=0;B.info.render.vertices=0;B.info.render.faces=0;B.info.render.points=0;this.setRenderTarget(c);(this.autoClear||d)&&this.clear(this.autoClearColor,this.autoClearDepth,this.autoClearStencil);d=0;for(var n=qa.length;d<n;d++){var m=qa[d],q=m.object;q.visible&&(w(q,b),p(m))}a.overrideMaterial?(d=a.overrideMaterial,u(d),"
                << std::endl ;
            out
                << "k(Ka,b,ca,g,d),k(Qa,b,ca,g,d),l(qa,\"\",b,ca,g,d)):(W.setBlending(THREE.NoBlending),k(Ka,b,ca,g,null),l(qa,\"opaque\",b,ca,g,null),k(Qa,b,ca,g,null),l(qa,\"transparent\",b,ca,g,null));Ac.render(a,b);Bc.render(a,b,Xb,fc);c&&c.generateMipmaps&&c.minFilter!==THREE.NearestFilter&&c.minFilter!==THREE.LinearFilter&&F(c);W.setDepthTest(!0);W.setDepthWrite(!0);W.setColorWrite(!0)}};this.renderImmediateObject=function(a,b,c,d,e){var f=v(a,b,c,d,e);ta=\"\";B.setMaterialFaces(d);e.immediateRenderCallback?e.immediateRenderCallback(f,"
                << std::endl ;
            out
                << "m,cb):e.render(function(a){B.renderBufferImmediate(a,f,d)})};var Ua={},Qb=0,pc={MeshDepthMaterial:\"depth\",MeshNormalMaterial:\"normal\",MeshBasicMaterial:\"basic\",MeshLambertMaterial:\"lambert\",MeshPhongMaterial:\"phong\",LineBasicMaterial:\"basic\",LineDashedMaterial:\"dashed\",PointCloudMaterial:\"particle_basic\"};this.setFaceCulling=function(a,b){a===THREE.CullFaceNone?m.disable(m.CULL_FACE):(b===THREE.FrontFaceDirectionCW?m.frontFace(m.CW):m.frontFace(m.CCW),a===THREE.CullFaceBack?m.cullFace(m.BACK):a==="
                << std::endl ;
            out
                << "THREE.CullFaceFront?m.cullFace(m.FRONT):m.cullFace(m.FRONT_AND_BACK),m.enable(m.CULL_FACE))};this.setMaterialFaces=function(a){W.setDoubleSided(a.side===THREE.DoubleSide);W.setFlipSided(a.side===THREE.BackSide)};this.uploadTexture=function(a){void 0===a.__webglInit&&(a.__webglInit=!0,a.addEventListener(\"dispose\",Pb),a.__webglTexture=m.createTexture(),B.info.memory.textures++);m.bindTexture(m.TEXTURE_2D,a.__webglTexture);m.pixelStorei(m.UNPACK_FLIP_Y_WEBGL,a.flipY);m.pixelStorei(m.UNPACK_PREMULTIPLY_ALPHA_WEBGL,"
                << std::endl ;
            out
                << "a.premultiplyAlpha);m.pixelStorei(m.UNPACK_ALIGNMENT,a.unpackAlignment);a.image=E(a.image,tc);var b=a.image,c=THREE.Math.isPowerOfTwo(b.width)&&THREE.Math.isPowerOfTwo(b.height),d=I(a.format),e=I(a.type);A(m.TEXTURE_2D,a,c);var f=a.mipmaps;if(a instanceof THREE.DataTexture)if(0<f.length&&c){for(var g=0,h=f.length;g<h;g++)b=f[g],m.texImage2D(m.TEXTURE_2D,g,d,b.width,b.height,0,d,e,b.data);a.generateMipmaps=!1}else m.texImage2D(m.TEXTURE_2D,0,d,b.width,b.height,0,d,e,b.data);else if(a instanceof THREE.CompressedTexture)for(g="
                << std::endl ;
            out
                << "0,h=f.length;g<h;g++)b=f[g],a.format!==THREE.RGBAFormat&&a.format!==THREE.RGBFormat?-1<kc().indexOf(d)?m.compressedTexImage2D(m.TEXTURE_2D,g,d,b.width,b.height,0,b.data):THREE.warn(\"THREE.WebGLRenderer: Attempt to load unsupported compressed texture format in .uploadTexture()\"):m.texImage2D(m.TEXTURE_2D,g,d,b.width,b.height,0,d,e,b.data);else if(0<f.length&&c){g=0;for(h=f.length;g<h;g++)b=f[g],m.texImage2D(m.TEXTURE_2D,g,d,d,e,b);a.generateMipmaps=!1}else m.texImage2D(m.TEXTURE_2D,0,d,d,e,a.image);"
                << std::endl ;
            out
                << "a.generateMipmaps&&c&&m.generateMipmap(m.TEXTURE_2D);a.needsUpdate=!1;if(a.onUpdate)a.onUpdate()};this.setTexture=function(a,b){m.activeTexture(m.TEXTURE0+b);a.needsUpdate?B.uploadTexture(a):m.bindTexture(m.TEXTURE_2D,a.__webglTexture)};this.setRenderTarget=function(a){var b=a instanceof THREE.WebGLRenderTargetCube;if(a&&void 0===a.__webglFramebuffer){void 0===a.depthBuffer&&(a.depthBuffer=!0);void 0===a.stencilBuffer&&(a.stencilBuffer=!0);a.addEventListener(\"dispose\",oc);a.__webglTexture=m.createTexture();"
                << std::endl ;
            out
                << "B.info.memory.textures++;var c=THREE.Math.isPowerOfTwo(a.width)&&THREE.Math.isPowerOfTwo(a.height),d=I(a.format),e=I(a.type);if(b){a.__webglFramebuffer=[];a.__webglRenderbuffer=[];m.bindTexture(m.TEXTURE_CUBE_MAP,a.__webglTexture);A(m.TEXTURE_CUBE_MAP,a,c);for(var f=0;6>f;f++){a.__webglFramebuffer[f]=m.createFramebuffer();a.__webglRenderbuffer[f]=m.createRenderbuffer();m.texImage2D(m.TEXTURE_CUBE_MAP_POSITIVE_X+f,0,d,a.width,a.height,0,d,e,null);var g=a,h=m.TEXTURE_CUBE_MAP_POSITIVE_X+f;m.bindFramebuffer(m.FRAMEBUFFER,"
                << std::endl ;
            out
                << "a.__webglFramebuffer[f]);m.framebufferTexture2D(m.FRAMEBUFFER,m.COLOR_ATTACHMENT0,h,g.__webglTexture,0);G(a.__webglRenderbuffer[f],a)}c&&m.generateMipmap(m.TEXTURE_CUBE_MAP)}else a.__webglFramebuffer=m.createFramebuffer(),a.__webglRenderbuffer=a.shareDepthFrom?a.shareDepthFrom.__webglRenderbuffer:m.createRenderbuffer(),m.bindTexture(m.TEXTURE_2D,a.__webglTexture),A(m.TEXTURE_2D,a,c),m.texImage2D(m.TEXTURE_2D,0,d,a.width,a.height,0,d,e,null),d=m.TEXTURE_2D,m.bindFramebuffer(m.FRAMEBUFFER,a.__webglFramebuffer),"
                << std::endl ;
            out
                << "m.framebufferTexture2D(m.FRAMEBUFFER,m.COLOR_ATTACHMENT0,d,a.__webglTexture,0),a.shareDepthFrom?a.depthBuffer&&!a.stencilBuffer?m.framebufferRenderbuffer(m.FRAMEBUFFER,m.DEPTH_ATTACHMENT,m.RENDERBUFFER,a.__webglRenderbuffer):a.depthBuffer&&a.stencilBuffer&&m.framebufferRenderbuffer(m.FRAMEBUFFER,m.DEPTH_STENCIL_ATTACHMENT,m.RENDERBUFFER,a.__webglRenderbuffer):G(a.__webglRenderbuffer,a),c&&m.generateMipmap(m.TEXTURE_2D);b?m.bindTexture(m.TEXTURE_CUBE_MAP,null):m.bindTexture(m.TEXTURE_2D,null);m.bindRenderbuffer(m.RENDERBUFFER,"
                << std::endl ;
            out
                << "null);m.bindFramebuffer(m.FRAMEBUFFER,null)}a?(b=b?a.__webglFramebuffer[a.activeCubeFace]:a.__webglFramebuffer,c=a.width,a=a.height,e=d=0):(b=null,c=pb,a=qb,d=ib,e=bb);b!==ab&&(m.bindFramebuffer(m.FRAMEBUFFER,b),m.viewport(d,e,c,a),ab=b);Xb=c;fc=a};this.readRenderTargetPixels=function(a,b,c,d,e,f){if(!(a instanceof THREE.WebGLRenderTarget))console.error(\"THREE.WebGLRenderer.readRenderTargetPixels: renderTarget is not THREE.WebGLRenderTarget.\");else if(a.__webglFramebuffer)if(a.format!==THREE.RGBAFormat)console.error(\"THREE.WebGLRenderer.readRenderTargetPixels: renderTarget is not in RGBA format. readPixels can read only RGBA format.\");"
                << std::endl ;
            out
                << "else{var g=!1;a.__webglFramebuffer!==ab&&(m.bindFramebuffer(m.FRAMEBUFFER,a.__webglFramebuffer),g=!0);m.checkFramebufferStatus(m.FRAMEBUFFER)===m.FRAMEBUFFER_COMPLETE?m.readPixels(b,c,d,e,m.RGBA,m.UNSIGNED_BYTE,f):console.error(\"THREE.WebGLRenderer.readRenderTargetPixels: readPixels from renderTarget failed. Framebuffer not complete.\");g&&m.bindFramebuffer(m.FRAMEBUFFER,ab)}};this.initMaterial=function(){THREE.warn(\"THREE.WebGLRenderer: .initMaterial() has been removed.\")};this.addPrePlugin=function(){THREE.warn(\"THREE.WebGLRenderer: .addPrePlugin() has been removed.\")};"
                << std::endl ;
            out
                << "this.addPostPlugin=function(){THREE.warn(\"THREE.WebGLRenderer: .addPostPlugin() has been removed.\")};this.updateShadowMap=function(){THREE.warn(\"THREE.WebGLRenderer: .updateShadowMap() has been removed.\")}};"
                << std::endl ;
            out
                << "THREE.WebGLRenderTarget=function(a,b,c){this.width=a;this.height=b;c=c||{};this.wrapS=void 0!==c.wrapS?c.wrapS:THREE.ClampToEdgeWrapping;this.wrapT=void 0!==c.wrapT?c.wrapT:THREE.ClampToEdgeWrapping;this.magFilter=void 0!==c.magFilter?c.magFilter:THREE.LinearFilter;this.minFilter=void 0!==c.minFilter?c.minFilter:THREE.LinearMipMapLinearFilter;this.anisotropy=void 0!==c.anisotropy?c.anisotropy:1;this.offset=new THREE.Vector2(0,0);this.repeat=new THREE.Vector2(1,1);this.format=void 0!==c.format?c.format:"
                << std::endl ;
            out
                << "THREE.RGBAFormat;this.type=void 0!==c.type?c.type:THREE.UnsignedByteType;this.depthBuffer=void 0!==c.depthBuffer?c.depthBuffer:!0;this.stencilBuffer=void 0!==c.stencilBuffer?c.stencilBuffer:!0;this.generateMipmaps=!0;this.shareDepthFrom=void 0!==c.shareDepthFrom?c.shareDepthFrom:null};"
                << std::endl ;
            out
                << "THREE.WebGLRenderTarget.prototype={constructor:THREE.WebGLRenderTarget,setSize:function(a,b){this.width=a;this.height=b},clone:function(){var a=new THREE.WebGLRenderTarget(this.width,this.height);a.wrapS=this.wrapS;a.wrapT=this.wrapT;a.magFilter=this.magFilter;a.minFilter=this.minFilter;a.anisotropy=this.anisotropy;a.offset.copy(this.offset);a.repeat.copy(this.repeat);a.format=this.format;a.type=this.type;a.depthBuffer=this.depthBuffer;a.stencilBuffer=this.stencilBuffer;a.generateMipmaps=this.generateMipmaps;"
                << std::endl ;
            out
                << "a.shareDepthFrom=this.shareDepthFrom;return a},dispose:function(){this.dispatchEvent({type:\"dispose\"})}};THREE.EventDispatcher.prototype.apply(THREE.WebGLRenderTarget.prototype);THREE.WebGLRenderTargetCube=function(a,b,c){THREE.WebGLRenderTarget.call(this,a,b,c);this.activeCubeFace=0};THREE.WebGLRenderTargetCube.prototype=Object.create(THREE.WebGLRenderTarget.prototype);THREE.WebGLRenderTargetCube.prototype.constructor=THREE.WebGLRenderTargetCube;"
                << std::endl ;
            out
                << "THREE.WebGLExtensions=function(a){var b={};this.get=function(c){if(void 0!==b[c])return b[c];var d;switch(c){case \"EXT_texture_filter_anisotropic\":d=a.getExtension(\"EXT_texture_filter_anisotropic\")||a.getExtension(\"MOZ_EXT_texture_filter_anisotropic\")||a.getExtension(\"WEBKIT_EXT_texture_filter_anisotropic\");break;case \"WEBGL_compressed_texture_s3tc\":d=a.getExtension(\"WEBGL_compressed_texture_s3tc\")||a.getExtension(\"MOZ_WEBGL_compressed_texture_s3tc\")||a.getExtension(\"WEBKIT_WEBGL_compressed_texture_s3tc\");"
                << std::endl ;
            out
                << "break;case \"WEBGL_compressed_texture_pvrtc\":d=a.getExtension(\"WEBGL_compressed_texture_pvrtc\")||a.getExtension(\"WEBKIT_WEBGL_compressed_texture_pvrtc\");break;default:d=a.getExtension(c)}null===d&&THREE.warn(\"THREE.WebGLRenderer: \"+c+\" extension not supported.\");return b[c]=d}};"
                << std::endl ;
            out
                << "THREE.WebGLProgram=function(){var a=0;return function(b,c,d,e){var f=b.context,g=d.defines,h=d.__webglShader.uniforms,k=d.attributes,l=d.__webglShader.vertexShader,p=d.__webglShader.fragmentShader,q=d.index0AttributeName;void 0===q&&!0===e.morphTargets&&(q=\"position\");var n=\"SHADOWMAP_TYPE_BASIC\";e.shadowMapType===THREE.PCFShadowMap?n=\"SHADOWMAP_TYPE_PCF\":e.shadowMapType===THREE.PCFSoftShadowMap&&(n=\"SHADOWMAP_TYPE_PCF_SOFT\");var t=\"ENVMAP_TYPE_CUBE\",r=\"ENVMAP_MODE_REFLECTION\",s=\"ENVMAP_BLENDING_MULTIPLY\";"
                << std::endl ;
            out
                << "if(e.envMap){switch(d.envMap.mapping){case THREE.CubeReflectionMapping:case THREE.CubeRefractionMapping:t=\"ENVMAP_TYPE_CUBE\";break;case THREE.EquirectangularReflectionMapping:case THREE.EquirectangularRefractionMapping:t=\"ENVMAP_TYPE_EQUIREC\";break;case THREE.SphericalReflectionMapping:t=\"ENVMAP_TYPE_SPHERE\"}switch(d.envMap.mapping){case THREE.CubeRefractionMapping:case THREE.EquirectangularRefractionMapping:r=\"ENVMAP_MODE_REFRACTION\"}switch(d.combine){case THREE.MultiplyOperation:s=\"ENVMAP_BLENDING_MULTIPLY\";"
                << std::endl ;
            out
                << "break;case THREE.MixOperation:s=\"ENVMAP_BLENDING_MIX\";break;case THREE.AddOperation:s=\"ENVMAP_BLENDING_ADD\"}}var u=0<b.gammaFactor?b.gammaFactor:1,v,x;v=[];for(var D in g)x=g[D],!1!==x&&(x=\"#define \"+D+\" \"+x,v.push(x));v=v.join(\"\\n\");g=f.createProgram();d instanceof THREE.RawShaderMaterial?b=d=\"\":(d=[\"precision \"+e.precision+\" float;\",\"precision \"+e.precision+\" int;\",v,e.supportsVertexTextures?\"#define VERTEX_TEXTURES\":\"\",b.gammaInput?\"#define GAMMA_INPUT\":\"\",b.gammaOutput?\"#define GAMMA_OUTPUT\":"
                << std::endl ;
            out
                << "\"\",\"#define GAMMA_FACTOR \"+u,\"#define MAX_DIR_LIGHTS \"+e.maxDirLights,\"#define MAX_POINT_LIGHTS \"+e.maxPointLights,\"#define MAX_SPOT_LIGHTS \"+e.maxSpotLights,\"#define MAX_HEMI_LIGHTS \"+e.maxHemiLights,\"#define MAX_SHADOWS \"+e.maxShadows,\"#define MAX_BONES \"+e.maxBones,e.map?\"#define USE_MAP\":\"\",e.envMap?\"#define USE_ENVMAP\":\"\",e.envMap?\"#define \"+r:\"\",e.lightMap?\"#define USE_LIGHTMAP\":\"\",e.bumpMap?\"#define USE_BUMPMAP\":\"\",e.normalMap?\"#define USE_NORMALMAP\":\"\",e.specularMap?\"#define USE_SPECULARMAP\":"
                << std::endl ;
            out
                << "\"\",e.alphaMap?\"#define USE_ALPHAMAP\":\"\",e.vertexColors?\"#define USE_COLOR\":\"\",e.flatShading?\"#define FLAT_SHADED\":\"\",e.skinning?\"#define USE_SKINNING\":\"\",e.useVertexTexture?\"#define BONE_TEXTURE\":\"\",e.morphTargets?\"#define USE_MORPHTARGETS\":\"\",e.morphNormals?\"#define USE_MORPHNORMALS\":\"\",e.wrapAround?\"#define WRAP_AROUND\":\"\",e.doubleSided?\"#define DOUBLE_SIDED\":\"\",e.flipSided?\"#define FLIP_SIDED\":\"\",e.shadowMapEnabled?\"#define USE_SHADOWMAP\":\"\",e.shadowMapEnabled?\"#define \"+n:\"\",e.shadowMapDebug?"
                << std::endl ;
            out
                << "\"#define SHADOWMAP_DEBUG\":\"\",e.shadowMapCascade?\"#define SHADOWMAP_CASCADE\":\"\",e.sizeAttenuation?\"#define USE_SIZEATTENUATION\":\"\",e.logarithmicDepthBuffer?\"#define USE_LOGDEPTHBUF\":\"\",\"uniform mat4 modelMatrix;\\nuniform mat4 modelViewMatrix;\\nuniform mat4 projectionMatrix;\\nuniform mat4 viewMatrix;\\nuniform mat3 normalMatrix;\\nuniform vec3 cameraPosition;\\nattribute vec3 position;\\nattribute vec3 normal;\\nattribute vec2 uv;\\nattribute vec2 uv2;\\n#ifdef USE_COLOR\\n\\tattribute vec3 color;\\n#endif\\n#ifdef USE_MORPHTARGETS\\n\\tattribute vec3 morphTarget0;\\n\\tattribute vec3 morphTarget1;\\n\\tattribute vec3 morphTarget2;\\n\\tattribute vec3 morphTarget3;\\n\\t#ifdef USE_MORPHNORMALS\\n\\t\\tattribute vec3 morphNormal0;\\n\\t\\tattribute vec3 morphNormal1;\\n\\t\\tattribute vec3 morphNormal2;\\n\\t\\tattribute vec3 morphNormal3;\\n\\t#else\\n\\t\\tattribute vec3 morphTarget4;\\n\\t\\tattribute vec3 morphTarget5;\\n\\t\\tattribute vec3 morphTarget6;\\n\\t\\tattribute vec3 morphTarget7;\\n\\t#endif\\n#endif\\n#ifdef USE_SKINNING\\n\\tattribute vec4 skinIndex;\\n\\tattribute vec4 skinWeight;\\n#endif\\n\"].join(\"\\n\"),"
                << std::endl ;
            out
                << "b=[\"precision \"+e.precision+\" float;\",\"precision \"+e.precision+\" int;\",e.bumpMap||e.normalMap||e.flatShading?\"#extension GL_OES_standard_derivatives : enable\":\"\",v,\"#define MAX_DIR_LIGHTS \"+e.maxDirLights,\"#define MAX_POINT_LIGHTS \"+e.maxPointLights,\"#define MAX_SPOT_LIGHTS \"+e.maxSpotLights,\"#define MAX_HEMI_LIGHTS \"+e.maxHemiLights,\"#define MAX_SHADOWS \"+e.maxShadows,e.alphaTest?\"#define ALPHATEST \"+e.alphaTest:\"\",b.gammaInput?\"#define GAMMA_INPUT\":\"\",b.gammaOutput?\"#define GAMMA_OUTPUT\":\"\",\"#define GAMMA_FACTOR \"+"
                << std::endl ;
            out
                << "u,e.useFog&&e.fog?\"#define USE_FOG\":\"\",e.useFog&&e.fogExp?\"#define FOG_EXP2\":\"\",e.map?\"#define USE_MAP\":\"\",e.envMap?\"#define USE_ENVMAP\":\"\",e.envMap?\"#define \"+t:\"\",e.envMap?\"#define \"+r:\"\",e.envMap?\"#define \"+s:\"\",e.lightMap?\"#define USE_LIGHTMAP\":\"\",e.bumpMap?\"#define USE_BUMPMAP\":\"\",e.normalMap?\"#define USE_NORMALMAP\":\"\",e.specularMap?\"#define USE_SPECULARMAP\":\"\",e.alphaMap?\"#define USE_ALPHAMAP\":\"\",e.vertexColors?\"#define USE_COLOR\":\"\",e.flatShading?\"#define FLAT_SHADED\":\"\",e.metal?\"#define METAL\":"
                << std::endl ;
            out
                << "\"\",e.wrapAround?\"#define WRAP_AROUND\":\"\",e.doubleSided?\"#define DOUBLE_SIDED\":\"\",e.flipSided?\"#define FLIP_SIDED\":\"\",e.shadowMapEnabled?\"#define USE_SHADOWMAP\":\"\",e.shadowMapEnabled?\"#define \"+n:\"\",e.shadowMapDebug?\"#define SHADOWMAP_DEBUG\":\"\",e.shadowMapCascade?\"#define SHADOWMAP_CASCADE\":\"\",e.logarithmicDepthBuffer?\"#define USE_LOGDEPTHBUF\":\"\",\"uniform mat4 viewMatrix;\\nuniform vec3 cameraPosition;\\n\"].join(\"\\n\"));l=new THREE.WebGLShader(f,f.VERTEX_SHADER,d+l);p=new THREE.WebGLShader(f,f.FRAGMENT_SHADER,"
                << std::endl ;
            out
                << "b+p);f.attachShader(g,l);f.attachShader(g,p);void 0!==q&&f.bindAttribLocation(g,0,q);f.linkProgram(g);q=f.getProgramInfoLog(g);!1===f.getProgramParameter(g,f.LINK_STATUS)&&THREE.error(\"THREE.WebGLProgram: shader error: \"+f.getError(),\"gl.VALIDATE_STATUS\",f.getProgramParameter(g,f.VALIDATE_STATUS),\"gl.getPRogramInfoLog\",q);\"\"!==q&&THREE.warn(\"THREE.WebGLProgram: gl.getProgramInfoLog()\"+q);f.deleteShader(l);f.deleteShader(p);q=\"viewMatrix modelViewMatrix projectionMatrix normalMatrix modelMatrix cameraPosition morphTargetInfluences bindMatrix bindMatrixInverse\".split(\" \");"
                << std::endl ;
            out
                << "e.useVertexTexture?(q.push(\"boneTexture\"),q.push(\"boneTextureWidth\"),q.push(\"boneTextureHeight\")):q.push(\"boneGlobalMatrices\");e.logarithmicDepthBuffer&&q.push(\"logDepthBufFC\");for(var w in h)q.push(w);h=q;w={};q=0;for(b=h.length;q<b;q++)n=h[q],w[n]=f.getUniformLocation(g,n);this.uniforms=w;q=\"position normal uv uv2 tangent color skinIndex skinWeight lineDistance\".split(\" \");for(h=0;h<e.maxMorphTargets;h++)q.push(\"morphTarget\"+h);for(h=0;h<e.maxMorphNormals;h++)q.push(\"morphNormal\"+h);for(var y in k)q.push(y);"
                << std::endl ;
            out
                << "e=q;k={};y=0;for(h=e.length;y<h;y++)w=e[y],k[w]=f.getAttribLocation(g,w);this.attributes=k;this.attributesKeys=Object.keys(this.attributes);this.id=a++;this.code=c;this.usedTimes=1;this.program=g;this.vertexShader=l;this.fragmentShader=p;return this}}();"
                << std::endl ;
            out
                << "THREE.WebGLShader=function(){var a=function(a){a=a.split(\"\\n\");for(var c=0;c<a.length;c++)a[c]=c+1+\": \"+a[c];return a.join(\"\\n\")};return function(b,c,d){c=b.createShader(c);b.shaderSource(c,d);b.compileShader(c);!1===b.getShaderParameter(c,b.COMPILE_STATUS)&&THREE.error(\"THREE.WebGLShader: Shader couldn't compile.\");\"\"!==b.getShaderInfoLog(c)&&THREE.warn(\"THREE.WebGLShader: gl.getShaderInfoLog()\",b.getShaderInfoLog(c),a(d));return c}}();"
                << std::endl ;
            out
                << "THREE.WebGLState=function(a,b){var c=new Uint8Array(16),d=new Uint8Array(16),e=null,f=null,g=null,h=null,k=null,l=null,p=null,q=null,n=null,t=null,r=null,s=null,u=null,v=null,x=null,D=null;this.initAttributes=function(){for(var a=0,b=c.length;a<b;a++)c[a]=0};this.enableAttribute=function(b){c[b]=1;0===d[b]&&(a.enableVertexAttribArray(b),d[b]=1)};this.disableUnusedAttributes=function(){for(var b=0,e=d.length;b<e;b++)d[b]!==c[b]&&(a.disableVertexAttribArray(b),d[b]=0)};this.setBlending=function(c,d,"
                << std::endl ;
            out
                << "n,q,r,s,t){c!==e&&(c===THREE.NoBlending?a.disable(a.BLEND):c===THREE.AdditiveBlending?(a.enable(a.BLEND),a.blendEquation(a.FUNC_ADD),a.blendFunc(a.SRC_ALPHA,a.ONE)):c===THREE.SubtractiveBlending?(a.enable(a.BLEND),a.blendEquation(a.FUNC_ADD),a.blendFunc(a.ZERO,a.ONE_MINUS_SRC_COLOR)):c===THREE.MultiplyBlending?(a.enable(a.BLEND),a.blendEquation(a.FUNC_ADD),a.blendFunc(a.ZERO,a.SRC_COLOR)):c===THREE.CustomBlending?a.enable(a.BLEND):(a.enable(a.BLEND),a.blendEquationSeparate(a.FUNC_ADD,a.FUNC_ADD),"
                << std::endl ;
            out
                << "a.blendFuncSeparate(a.SRC_ALPHA,a.ONE_MINUS_SRC_ALPHA,a.ONE,a.ONE_MINUS_SRC_ALPHA)),e=c);if(c===THREE.CustomBlending){r=r||d;s=s||n;t=t||q;if(d!==f||r!==k)a.blendEquationSeparate(b(d),b(r)),f=d,k=r;if(n!==g||q!==h||s!==l||t!==p)a.blendFuncSeparate(b(n),b(q),b(s),b(t)),g=n,h=q,l=s,p=t}else p=l=k=h=g=f=null};this.setDepthTest=function(b){q!==b&&(b?a.enable(a.DEPTH_TEST):a.disable(a.DEPTH_TEST),q=b)};this.setDepthWrite=function(b){n!==b&&(a.depthMask(b),n=b)};this.setColorWrite=function(b){t!==b&&(a.colorMask(b,"
                << std::endl ;
            out
                << "b,b,b),t=b)};this.setDoubleSided=function(b){r!==b&&(b?a.disable(a.CULL_FACE):a.enable(a.CULL_FACE),r=b)};this.setFlipSided=function(b){s!==b&&(b?a.frontFace(a.CW):a.frontFace(a.CCW),s=b)};this.setLineWidth=function(b){b!==u&&(a.lineWidth(b),u=b)};this.setPolygonOffset=function(b,c,d){v!==b&&(b?a.enable(a.POLYGON_OFFSET_FILL):a.disable(a.POLYGON_OFFSET_FILL),v=b);!b||x===c&&D===d||(a.polygonOffset(c,d),x=c,D=d)};this.reset=function(){for(var a=0;a<d.length;a++)d[a]=0;s=r=t=n=q=e=null}};"
                << std::endl ;
            out
                << "THREE.LensFlarePlugin=function(a,b){var c,d,e,f,g,h,k,l,p,q,n=a.context,t,r,s,u,v,x;this.render=function(D,w,y,A){if(0!==b.length){D=new THREE.Vector3;var E=A/y,G=.5*y,F=.5*A,z=16/A,I=new THREE.Vector2(z*E,z),U=new THREE.Vector3(1,1,0),M=new THREE.Vector2(1,1);if(void 0===s){var z=new Float32Array([-1,-1,0,0,1,-1,1,0,1,1,1,1,-1,1,0,1]),H=new Uint16Array([0,1,2,0,2,3]);t=n.createBuffer();r=n.createBuffer();n.bindBuffer(n.ARRAY_BUFFER,t);n.bufferData(n.ARRAY_BUFFER,z,n.STATIC_DRAW);n.bindBuffer(n.ELEMENT_ARRAY_BUFFER,"
                << std::endl ;
            out
                << "r);n.bufferData(n.ELEMENT_ARRAY_BUFFER,H,n.STATIC_DRAW);v=n.createTexture();x=n.createTexture();n.bindTexture(n.TEXTURE_2D,v);n.texImage2D(n.TEXTURE_2D,0,n.RGB,16,16,0,n.RGB,n.UNSIGNED_BYTE,null);n.texParameteri(n.TEXTURE_2D,n.TEXTURE_WRAP_S,n.CLAMP_TO_EDGE);n.texParameteri(n.TEXTURE_2D,n.TEXTURE_WRAP_T,n.CLAMP_TO_EDGE);n.texParameteri(n.TEXTURE_2D,n.TEXTURE_MAG_FILTER,n.NEAREST);n.texParameteri(n.TEXTURE_2D,n.TEXTURE_MIN_FILTER,n.NEAREST);n.bindTexture(n.TEXTURE_2D,x);n.texImage2D(n.TEXTURE_2D,0,"
                << std::endl ;
            out
                << "n.RGBA,16,16,0,n.RGBA,n.UNSIGNED_BYTE,null);n.texParameteri(n.TEXTURE_2D,n.TEXTURE_WRAP_S,n.CLAMP_TO_EDGE);n.texParameteri(n.TEXTURE_2D,n.TEXTURE_WRAP_T,n.CLAMP_TO_EDGE);n.texParameteri(n.TEXTURE_2D,n.TEXTURE_MAG_FILTER,n.NEAREST);n.texParameteri(n.TEXTURE_2D,n.TEXTURE_MIN_FILTER,n.NEAREST);var z=(u=0<n.getParameter(n.MAX_VERTEX_TEXTURE_IMAGE_UNITS))?{vertexShader:\"uniform lowp int renderType;\\nuniform vec3 screenPosition;\\nuniform vec2 scale;\\nuniform float rotation;\\nuniform sampler2D occlusionMap;\\nattribute vec2 position;\\nattribute vec2 uv;\\nvarying vec2 vUV;\\nvarying float vVisibility;\\nvoid main() {\\nvUV = uv;\\nvec2 pos = position;\\nif( renderType == 2 ) {\\nvec4 visibility = texture2D( occlusionMap, vec2( 0.1, 0.1 ) );\\nvisibility += texture2D( occlusionMap, vec2( 0.5, 0.1 ) );\\nvisibility += texture2D( occlusionMap, vec2( 0.9, 0.1 ) );\\nvisibility += texture2D( occlusionMap, vec2( 0.9, 0.5 ) );\\nvisibility += texture2D( occlusionMap, vec2( 0.9, 0.9 ) );\\nvisibility += texture2D( occlusionMap, vec2( 0.5, 0.9 ) );\\nvisibility += texture2D( occlusionMap, vec2( 0.1, 0.9 ) );\\nvisibility += texture2D( occlusionMap, vec2( 0.1, 0.5 ) );\\nvisibility += texture2D( occlusionMap, vec2( 0.5, 0.5 ) );\\nvVisibility =        visibility.r / 9.0;\\nvVisibility *= 1.0 - visibility.g / 9.0;\\nvVisibility *=       visibility.b / 9.0;\\nvVisibility *= 1.0 - visibility.a / 9.0;\\npos.x = cos( rotation ) * position.x - sin( rotation ) * position.y;\\npos.y = sin( rotation ) * position.x + cos( rotation ) * position.y;\\n}\\ngl_Position = vec4( ( pos * scale + screenPosition.xy ).xy, screenPosition.z, 1.0 );\\n}\","
                << std::endl ;
            out
                << "fragmentShader:\"uniform lowp int renderType;\\nuniform sampler2D map;\\nuniform float opacity;\\nuniform vec3 color;\\nvarying vec2 vUV;\\nvarying float vVisibility;\\nvoid main() {\\nif( renderType == 0 ) {\\ngl_FragColor = vec4( 1.0, 0.0, 1.0, 0.0 );\\n} else if( renderType == 1 ) {\\ngl_FragColor = texture2D( map, vUV );\\n} else {\\nvec4 texture = texture2D( map, vUV );\\ntexture.a *= opacity * vVisibility;\\ngl_FragColor = texture;\\ngl_FragColor.rgb *= color;\\n}\\n}\"}:{vertexShader:\"uniform lowp int renderType;\\nuniform vec3 screenPosition;\\nuniform vec2 scale;\\nuniform float rotation;\\nattribute vec2 position;\\nattribute vec2 uv;\\nvarying vec2 vUV;\\nvoid main() {\\nvUV = uv;\\nvec2 pos = position;\\nif( renderType == 2 ) {\\npos.x = cos( rotation ) * position.x - sin( rotation ) * position.y;\\npos.y = sin( rotation ) * position.x + cos( rotation ) * position.y;\\n}\\ngl_Position = vec4( ( pos * scale + screenPosition.xy ).xy, screenPosition.z, 1.0 );\\n}\","
                << std::endl ;
            out
                << "fragmentShader:\"precision mediump float;\\nuniform lowp int renderType;\\nuniform sampler2D map;\\nuniform sampler2D occlusionMap;\\nuniform float opacity;\\nuniform vec3 color;\\nvarying vec2 vUV;\\nvoid main() {\\nif( renderType == 0 ) {\\ngl_FragColor = vec4( texture2D( map, vUV ).rgb, 0.0 );\\n} else if( renderType == 1 ) {\\ngl_FragColor = texture2D( map, vUV );\\n} else {\\nfloat visibility = texture2D( occlusionMap, vec2( 0.5, 0.1 ) ).a;\\nvisibility += texture2D( occlusionMap, vec2( 0.9, 0.5 ) ).a;\\nvisibility += texture2D( occlusionMap, vec2( 0.5, 0.9 ) ).a;\\nvisibility += texture2D( occlusionMap, vec2( 0.1, 0.5 ) ).a;\\nvisibility = ( 1.0 - visibility / 4.0 );\\nvec4 texture = texture2D( map, vUV );\\ntexture.a *= opacity * visibility;\\ngl_FragColor = texture;\\ngl_FragColor.rgb *= color;\\n}\\n}\"},"
                << std::endl ;
            out
                << "H=n.createProgram(),L=n.createShader(n.FRAGMENT_SHADER),P=n.createShader(n.VERTEX_SHADER),N=\"precision \"+a.getPrecision()+\" float;\\n\";n.shaderSource(L,N+z.fragmentShader);n.shaderSource(P,N+z.vertexShader);n.compileShader(L);n.compileShader(P);n.attachShader(H,L);n.attachShader(H,P);n.linkProgram(H);s=H;p=n.getAttribLocation(s,\"position\");q=n.getAttribLocation(s,\"uv\");c=n.getUniformLocation(s,\"renderType\");d=n.getUniformLocation(s,\"map\");e=n.getUniformLocation(s,\"occlusionMap\");f=n.getUniformLocation(s,"
                << std::endl ;
            out
                << "\"opacity\");g=n.getUniformLocation(s,\"color\");h=n.getUniformLocation(s,\"scale\");k=n.getUniformLocation(s,\"rotation\");l=n.getUniformLocation(s,\"screenPosition\")}n.useProgram(s);n.enableVertexAttribArray(p);n.enableVertexAttribArray(q);n.uniform1i(e,0);n.uniform1i(d,1);n.bindBuffer(n.ARRAY_BUFFER,t);n.vertexAttribPointer(p,2,n.FLOAT,!1,16,0);n.vertexAttribPointer(q,2,n.FLOAT,!1,16,8);n.bindBuffer(n.ELEMENT_ARRAY_BUFFER,r);n.disable(n.CULL_FACE);n.depthMask(!1);H=0;for(L=b.length;H<L;H++)if(z=16/A,I.set(z*"
                << std::endl ;
            out
                << "E,z),P=b[H],D.set(P.matrixWorld.elements[12],P.matrixWorld.elements[13],P.matrixWorld.elements[14]),D.applyMatrix4(w.matrixWorldInverse),D.applyProjection(w.projectionMatrix),U.copy(D),M.x=U.x*G+G,M.y=U.y*F+F,u||0<M.x&&M.x<y&&0<M.y&&M.y<A){n.activeTexture(n.TEXTURE1);n.bindTexture(n.TEXTURE_2D,v);n.copyTexImage2D(n.TEXTURE_2D,0,n.RGB,M.x-8,M.y-8,16,16,0);n.uniform1i(c,0);n.uniform2f(h,I.x,I.y);n.uniform3f(l,U.x,U.y,U.z);n.disable(n.BLEND);n.enable(n.DEPTH_TEST);n.drawElements(n.TRIANGLES,6,n.UNSIGNED_SHORT,"
                << std::endl ;
            out
                << "0);n.activeTexture(n.TEXTURE0);n.bindTexture(n.TEXTURE_2D,x);n.copyTexImage2D(n.TEXTURE_2D,0,n.RGBA,M.x-8,M.y-8,16,16,0);n.uniform1i(c,1);n.disable(n.DEPTH_TEST);n.activeTexture(n.TEXTURE1);n.bindTexture(n.TEXTURE_2D,v);n.drawElements(n.TRIANGLES,6,n.UNSIGNED_SHORT,0);P.positionScreen.copy(U);P.customUpdateCallback?P.customUpdateCallback(P):P.updateLensFlares();n.uniform1i(c,2);n.enable(n.BLEND);for(var N=0,R=P.lensFlares.length;N<R;N++){var V=P.lensFlares[N];.001<V.opacity&&.001<V.scale&&(U.x=V.x,"
                << std::endl ;
            out
                << "U.y=V.y,U.z=V.z,z=V.size*V.scale/A,I.x=z*E,I.y=z,n.uniform3f(l,U.x,U.y,U.z),n.uniform2f(h,I.x,I.y),n.uniform1f(k,V.rotation),n.uniform1f(f,V.opacity),n.uniform3f(g,V.color.r,V.color.g,V.color.b),a.state.setBlending(V.blending,V.blendEquation,V.blendSrc,V.blendDst),a.setTexture(V.texture,1),n.drawElements(n.TRIANGLES,6,n.UNSIGNED_SHORT,0))}}n.enable(n.CULL_FACE);n.enable(n.DEPTH_TEST);n.depthMask(!0);a.resetGLState()}}};"
                << std::endl ;
            out
                << "THREE.ShadowMapPlugin=function(a,b,c,d){function e(a,b,d){if(b.visible){var f=c[b.id];if(f&&b.castShadow&&(!1===b.frustumCulled||!0===p.intersectsObject(b)))for(var g=0,h=f.length;g<h;g++){var k=f[g];b._modelViewMatrix.multiplyMatrices(d.matrixWorldInverse,b.matrixWorld);s.push(k)}g=0;for(h=b.children.length;g<h;g++)e(a,b.children[g],d)}}var f=a.context,g,h,k,l,p=new THREE.Frustum,q=new THREE.Matrix4,n=new THREE.Vector3,t=new THREE.Vector3,r=new THREE.Vector3,s=[],u=THREE.ShaderLib.depthRGBA,v=THREE.UniformsUtils.clone(u.uniforms);"
                << std::endl ;
            out
                << "g=new THREE.ShaderMaterial({uniforms:v,vertexShader:u.vertexShader,fragmentShader:u.fragmentShader});h=new THREE.ShaderMaterial({uniforms:v,vertexShader:u.vertexShader,fragmentShader:u.fragmentShader,morphTargets:!0});k=new THREE.ShaderMaterial({uniforms:v,vertexShader:u.vertexShader,fragmentShader:u.fragmentShader,skinning:!0});l=new THREE.ShaderMaterial({uniforms:v,vertexShader:u.vertexShader,fragmentShader:u.fragmentShader,morphTargets:!0,skinning:!0});g._shadowPass=!0;h._shadowPass=!0;k._shadowPass="
                << std::endl ;
            out
                << "!0;l._shadowPass=!0;this.render=function(c,v){if(!1!==a.shadowMapEnabled){var u,y,A,E,G,F,z,I,U=[];E=0;f.clearColor(1,1,1,1);f.disable(f.BLEND);f.enable(f.CULL_FACE);f.frontFace(f.CCW);a.shadowMapCullFace===THREE.CullFaceFront?f.cullFace(f.FRONT):f.cullFace(f.BACK);a.state.setDepthTest(!0);u=0;for(y=b.length;u<y;u++)if(A=b[u],A.castShadow)if(A instanceof THREE.DirectionalLight&&A.shadowCascade)for(G=0;G<A.shadowCascadeCount;G++){var M;if(A.shadowCascadeArray[G])M=A.shadowCascadeArray[G];else{z=A;"
                << std::endl ;
            out
                << "var H=G;M=new THREE.DirectionalLight;M.isVirtual=!0;M.onlyShadow=!0;M.castShadow=!0;M.shadowCameraNear=z.shadowCameraNear;M.shadowCameraFar=z.shadowCameraFar;M.shadowCameraLeft=z.shadowCameraLeft;M.shadowCameraRight=z.shadowCameraRight;M.shadowCameraBottom=z.shadowCameraBottom;M.shadowCameraTop=z.shadowCameraTop;M.shadowCameraVisible=z.shadowCameraVisible;M.shadowDarkness=z.shadowDarkness;M.shadowBias=z.shadowCascadeBias[H];M.shadowMapWidth=z.shadowCascadeWidth[H];M.shadowMapHeight=z.shadowCascadeHeight[H];"
                << std::endl ;
            out
                << "M.pointsWorld=[];M.pointsFrustum=[];I=M.pointsWorld;F=M.pointsFrustum;for(var L=0;8>L;L++)I[L]=new THREE.Vector3,F[L]=new THREE.Vector3;I=z.shadowCascadeNearZ[H];z=z.shadowCascadeFarZ[H];F[0].set(-1,-1,I);F[1].set(1,-1,I);F[2].set(-1,1,I);F[3].set(1,1,I);F[4].set(-1,-1,z);F[5].set(1,-1,z);F[6].set(-1,1,z);F[7].set(1,1,z);M.originalCamera=v;F=new THREE.Gyroscope;F.position.copy(A.shadowCascadeOffset);F.add(M);F.add(M.target);v.add(F);A.shadowCascadeArray[G]=M}H=A;I=G;z=H.shadowCascadeArray[I];z.position.copy(H.position);"
                << std::endl ;
            out
                << "z.target.position.copy(H.target.position);z.lookAt(z.target);z.shadowCameraVisible=H.shadowCameraVisible;z.shadowDarkness=H.shadowDarkness;z.shadowBias=H.shadowCascadeBias[I];F=H.shadowCascadeNearZ[I];H=H.shadowCascadeFarZ[I];z=z.pointsFrustum;z[0].z=F;z[1].z=F;z[2].z=F;z[3].z=F;z[4].z=H;z[5].z=H;z[6].z=H;z[7].z=H;U[E]=M;E++}else U[E]=A,E++;u=0;for(y=U.length;u<y;u++){A=U[u];A.shadowMap||(G=THREE.LinearFilter,a.shadowMapType===THREE.PCFSoftShadowMap&&(G=THREE.NearestFilter),A.shadowMap=new THREE.WebGLRenderTarget(A.shadowMapWidth,"
                << std::endl ;
            out
                << "A.shadowMapHeight,{minFilter:G,magFilter:G,format:THREE.RGBAFormat}),A.shadowMapSize=new THREE.Vector2(A.shadowMapWidth,A.shadowMapHeight),A.shadowMatrix=new THREE.Matrix4);if(!A.shadowCamera){if(A instanceof THREE.SpotLight)A.shadowCamera=new THREE.PerspectiveCamera(A.shadowCameraFov,A.shadowMapWidth/A.shadowMapHeight,A.shadowCameraNear,A.shadowCameraFar);else if(A instanceof THREE.DirectionalLight)A.shadowCamera=new THREE.OrthographicCamera(A.shadowCameraLeft,A.shadowCameraRight,A.shadowCameraTop,"
                << std::endl ;
            out
                << "A.shadowCameraBottom,A.shadowCameraNear,A.shadowCameraFar);else{THREE.error(\"THREE.ShadowMapPlugin: Unsupported light type for shadow\",A);continue}c.add(A.shadowCamera);!0===c.autoUpdate&&c.updateMatrixWorld()}A.shadowCameraVisible&&!A.cameraHelper&&(A.cameraHelper=new THREE.CameraHelper(A.shadowCamera),c.add(A.cameraHelper));if(A.isVirtual&&M.originalCamera==v){G=v;E=A.shadowCamera;F=A.pointsFrustum;z=A.pointsWorld;n.set(Infinity,Infinity,Infinity);t.set(-Infinity,-Infinity,-Infinity);for(H=0;8>"
                << std::endl ;
            out
                << "H;H++)I=z[H],I.copy(F[H]),I.unproject(G),I.applyMatrix4(E.matrixWorldInverse),I.x<n.x&&(n.x=I.x),I.x>t.x&&(t.x=I.x),I.y<n.y&&(n.y=I.y),I.y>t.y&&(t.y=I.y),I.z<n.z&&(n.z=I.z),I.z>t.z&&(t.z=I.z);E.left=n.x;E.right=t.x;E.top=t.y;E.bottom=n.y;E.updateProjectionMatrix()}E=A.shadowMap;F=A.shadowMatrix;G=A.shadowCamera;G.position.setFromMatrixPosition(A.matrixWorld);r.setFromMatrixPosition(A.target.matrixWorld);G.lookAt(r);G.updateMatrixWorld();G.matrixWorldInverse.getInverse(G.matrixWorld);A.cameraHelper&&"
                << std::endl ;
            out
                << "(A.cameraHelper.visible=A.shadowCameraVisible);A.shadowCameraVisible&&A.cameraHelper.update();F.set(.5,0,0,.5,0,.5,0,.5,0,0,.5,.5,0,0,0,1);F.multiply(G.projectionMatrix);F.multiply(G.matrixWorldInverse);q.multiplyMatrices(G.projectionMatrix,G.matrixWorldInverse);p.setFromMatrix(q);a.setRenderTarget(E);a.clear();s.length=0;e(c,c,G);A=0;for(E=s.length;A<E;A++)z=s[A],F=z.object,z=z.buffer,H=F.material instanceof THREE.MeshFaceMaterial?F.material.materials[0]:F.material,I=void 0!==F.geometry.morphTargets&&"
                << std::endl ;
            out
                << "0<F.geometry.morphTargets.length&&H.morphTargets,L=F instanceof THREE.SkinnedMesh&&H.skinning,I=F.customDepthMaterial?F.customDepthMaterial:L?I?l:k:I?h:g,a.setMaterialFaces(H),z instanceof THREE.BufferGeometry?a.renderBufferDirect(G,b,null,I,z,F):a.renderBuffer(G,b,null,I,z,F);A=0;for(E=d.length;A<E;A++)z=d[A],F=z.object,F.visible&&F.castShadow&&(F._modelViewMatrix.multiplyMatrices(G.matrixWorldInverse,F.matrixWorld),a.renderImmediateObject(G,b,null,g,F))}u=a.getClearColor();y=a.getClearAlpha();f.clearColor(u.r,"
                << std::endl ;
            out
                << "u.g,u.b,y);f.enable(f.BLEND);a.shadowMapCullFace===THREE.CullFaceFront&&f.cullFace(f.BACK);a.resetGLState()}}};"
                << std::endl ;
            out
                << "THREE.SpritePlugin=function(a,b){var c,d,e,f,g,h,k,l,p,q,n,t,r,s,u,v,x;function D(a,b){return a.z!==b.z?b.z-a.z:b.id-a.id}var w=a.context,y,A,E,G,F=new THREE.Vector3,z=new THREE.Quaternion,I=new THREE.Vector3;this.render=function(U,M){if(0!==b.length){if(void 0===E){var H=new Float32Array([-.5,-.5,0,0,.5,-.5,1,0,.5,.5,1,1,-.5,.5,0,1]),L=new Uint16Array([0,1,2,0,2,3]);y=w.createBuffer();A=w.createBuffer();w.bindBuffer(w.ARRAY_BUFFER,y);w.bufferData(w.ARRAY_BUFFER,H,w.STATIC_DRAW);w.bindBuffer(w.ELEMENT_ARRAY_BUFFER,"
                << std::endl ;
            out
                << "A);w.bufferData(w.ELEMENT_ARRAY_BUFFER,L,w.STATIC_DRAW);var H=w.createProgram(),L=w.createShader(w.VERTEX_SHADER),P=w.createShader(w.FRAGMENT_SHADER);w.shaderSource(L,[\"precision \"+a.getPrecision()+\" float;\",\"uniform mat4 modelViewMatrix;\\nuniform mat4 projectionMatrix;\\nuniform float rotation;\\nuniform vec2 scale;\\nuniform vec2 uvOffset;\\nuniform vec2 uvScale;\\nattribute vec2 position;\\nattribute vec2 uv;\\nvarying vec2 vUV;\\nvoid main() {\\nvUV = uvOffset + uv * uvScale;\\nvec2 alignedPosition = position * scale;\\nvec2 rotatedPosition;\\nrotatedPosition.x = cos( rotation ) * alignedPosition.x - sin( rotation ) * alignedPosition.y;\\nrotatedPosition.y = sin( rotation ) * alignedPosition.x + cos( rotation ) * alignedPosition.y;\\nvec4 finalPosition;\\nfinalPosition = modelViewMatrix * vec4( 0.0, 0.0, 0.0, 1.0 );\\nfinalPosition.xy += rotatedPosition;\\nfinalPosition = projectionMatrix * finalPosition;\\ngl_Position = finalPosition;\\n}\"].join(\"\\n\"));"
                << std::endl ;
            out
                << "w.shaderSource(P,[\"precision \"+a.getPrecision()+\" float;\",\"uniform vec3 color;\\nuniform sampler2D map;\\nuniform float opacity;\\nuniform int fogType;\\nuniform vec3 fogColor;\\nuniform float fogDensity;\\nuniform float fogNear;\\nuniform float fogFar;\\nuniform float alphaTest;\\nvarying vec2 vUV;\\nvoid main() {\\nvec4 texture = texture2D( map, vUV );\\nif ( texture.a < alphaTest ) discard;\\ngl_FragColor = vec4( color * texture.xyz, texture.a * opacity );\\nif ( fogType > 0 ) {\\nfloat depth = gl_FragCoord.z / gl_FragCoord.w;\\nfloat fogFactor = 0.0;\\nif ( fogType == 1 ) {\\nfogFactor = smoothstep( fogNear, fogFar, depth );\\n} else {\\nconst float LOG2 = 1.442695;\\nfloat fogFactor = exp2( - fogDensity * fogDensity * depth * depth * LOG2 );\\nfogFactor = 1.0 - clamp( fogFactor, 0.0, 1.0 );\\n}\\ngl_FragColor = mix( gl_FragColor, vec4( fogColor, gl_FragColor.w ), fogFactor );\\n}\\n}\"].join(\"\\n\"));"
                << std::endl ;
            out
                << "w.compileShader(L);w.compileShader(P);w.attachShader(H,L);w.attachShader(H,P);w.linkProgram(H);E=H;v=w.getAttribLocation(E,\"position\");x=w.getAttribLocation(E,\"uv\");c=w.getUniformLocation(E,\"uvOffset\");d=w.getUniformLocation(E,\"uvScale\");e=w.getUniformLocation(E,\"rotation\");f=w.getUniformLocation(E,\"scale\");g=w.getUniformLocation(E,\"color\");h=w.getUniformLocation(E,\"map\");k=w.getUniformLocation(E,\"opacity\");l=w.getUniformLocation(E,\"modelViewMatrix\");p=w.getUniformLocation(E,\"projectionMatrix\");q="
                << std::endl ;
            out
                << "w.getUniformLocation(E,\"fogType\");n=w.getUniformLocation(E,\"fogDensity\");t=w.getUniformLocation(E,\"fogNear\");r=w.getUniformLocation(E,\"fogFar\");s=w.getUniformLocation(E,\"fogColor\");u=w.getUniformLocation(E,\"alphaTest\");H=document.createElement(\"canvas\");H.width=8;H.height=8;L=H.getContext(\"2d\");L.fillStyle=\"white\";L.fillRect(0,0,8,8);G=new THREE.Texture(H);G.needsUpdate=!0}w.useProgram(E);w.enableVertexAttribArray(v);w.enableVertexAttribArray(x);w.disable(w.CULL_FACE);w.enable(w.BLEND);w.bindBuffer(w.ARRAY_BUFFER,"
                << std::endl ;
            out
                << "y);w.vertexAttribPointer(v,2,w.FLOAT,!1,16,0);w.vertexAttribPointer(x,2,w.FLOAT,!1,16,8);w.bindBuffer(w.ELEMENT_ARRAY_BUFFER,A);w.uniformMatrix4fv(p,!1,M.projectionMatrix.elements);w.activeTexture(w.TEXTURE0);w.uniform1i(h,0);L=H=0;(P=U.fog)?(w.uniform3f(s,P.color.r,P.color.g,P.color.b),P instanceof THREE.Fog?(w.uniform1f(t,P.near),w.uniform1f(r,P.far),w.uniform1i(q,1),L=H=1):P instanceof THREE.FogExp2&&(w.uniform1f(n,P.density),w.uniform1i(q,2),L=H=2)):(w.uniform1i(q,0),L=H=0);for(var P=0,N=b.length;P<"
                << std::endl ;
            out
                << "N;P++){var R=b[P];R._modelViewMatrix.multiplyMatrices(M.matrixWorldInverse,R.matrixWorld);R.z=-R._modelViewMatrix.elements[14]}b.sort(D);for(var V=[],P=0,N=b.length;P<N;P++){var R=b[P],J=R.material;w.uniform1f(u,J.alphaTest);w.uniformMatrix4fv(l,!1,R._modelViewMatrix.elements);R.matrixWorld.decompose(F,z,I);V[0]=I.x;V[1]=I.y;R=0;U.fog&&J.fog&&(R=L);H!==R&&(w.uniform1i(q,R),H=R);null!==J.map?(w.uniform2f(c,J.map.offset.x,J.map.offset.y),w.uniform2f(d,J.map.repeat.x,J.map.repeat.y)):(w.uniform2f(c,"
                << std::endl ;
            out
                << "0,0),w.uniform2f(d,1,1));w.uniform1f(k,J.opacity);w.uniform3f(g,J.color.r,J.color.g,J.color.b);w.uniform1f(e,J.rotation);w.uniform2fv(f,V);a.state.setBlending(J.blending,J.blendEquation,J.blendSrc,J.blendDst);a.state.setDepthTest(J.depthTest);a.state.setDepthWrite(J.depthWrite);J.map&&J.map.image&&J.map.image.width?a.setTexture(J.map,0):a.setTexture(G,0);w.drawElements(w.TRIANGLES,6,w.UNSIGNED_SHORT,0)}w.enable(w.CULL_FACE);a.resetGLState()}}};"
                << std::endl ;
            out
                << "THREE.GeometryUtils={merge:function(a,b,c){THREE.warn(\"THREE.GeometryUtils: .merge() has been moved to Geometry. Use geometry.merge( geometry2, matrix, materialIndexOffset ) instead.\");var d;b instanceof THREE.Mesh&&(b.matrixAutoUpdate&&b.updateMatrix(),d=b.matrix,b=b.geometry);a.merge(b,d,c)},center:function(a){THREE.warn(\"THREE.GeometryUtils: .center() has been moved to Geometry. Use geometry.center() instead.\");return a.center()}};"
                << std::endl ;
            out
                << "THREE.ImageUtils={crossOrigin:void 0,loadTexture:function(a,b,c,d){var e=new THREE.ImageLoader;e.crossOrigin=this.crossOrigin;var f=new THREE.Texture(void 0,b);e.load(a,function(a){f.image=a;f.needsUpdate=!0;c&&c(f)},void 0,function(a){d&&d(a)});f.sourceFile=a;return f},loadTextureCube:function(a,b,c,d){var e=new THREE.ImageLoader;e.crossOrigin=this.crossOrigin;var f=new THREE.CubeTexture([],b);f.flipY=!1;var g=0;b=function(b){e.load(a[b],function(a){f.images[b]=a;g+=1;6===g&&(f.needsUpdate=!0,c&&"
                << std::endl ;
            out
                << "c(f))},void 0,d)};for(var h=0,k=a.length;h<k;++h)b(h);return f},loadCompressedTexture:function(){THREE.error(\"THREE.ImageUtils.loadCompressedTexture has been removed. Use THREE.DDSLoader instead.\")},loadCompressedTextureCube:function(){THREE.error(\"THREE.ImageUtils.loadCompressedTextureCube has been removed. Use THREE.DDSLoader instead.\")},getNormalMap:function(a,b){var c=function(a){var b=Math.sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]);return[a[0]/b,a[1]/b,a[2]/b]};b|=1;var d=a.width,e=a.height,f=document.createElement(\"canvas\");"
                << std::endl ;
            out
                << "f.width=d;f.height=e;var g=f.getContext(\"2d\");g.drawImage(a,0,0);for(var h=g.getImageData(0,0,d,e).data,k=g.createImageData(d,e),l=k.data,p=0;p<d;p++)for(var q=0;q<e;q++){var n=0>q-1?0:q-1,t=q+1>e-1?e-1:q+1,r=0>p-1?0:p-1,s=p+1>d-1?d-1:p+1,u=[],v=[0,0,h[4*(q*d+p)]/255*b];u.push([-1,0,h[4*(q*d+r)]/255*b]);u.push([-1,-1,h[4*(n*d+r)]/255*b]);u.push([0,-1,h[4*(n*d+p)]/255*b]);u.push([1,-1,h[4*(n*d+s)]/255*b]);u.push([1,0,h[4*(q*d+s)]/255*b]);u.push([1,1,h[4*(t*d+s)]/255*b]);u.push([0,1,h[4*(t*d+p)]/255*"
                << std::endl ;
            out
                << "b]);u.push([-1,1,h[4*(t*d+r)]/255*b]);n=[];r=u.length;for(t=0;t<r;t++){var s=u[t],x=u[(t+1)%r],s=[s[0]-v[0],s[1]-v[1],s[2]-v[2]],x=[x[0]-v[0],x[1]-v[1],x[2]-v[2]];n.push(c([s[1]*x[2]-s[2]*x[1],s[2]*x[0]-s[0]*x[2],s[0]*x[1]-s[1]*x[0]]))}u=[0,0,0];for(t=0;t<n.length;t++)u[0]+=n[t][0],u[1]+=n[t][1],u[2]+=n[t][2];u[0]/=n.length;u[1]/=n.length;u[2]/=n.length;v=4*(q*d+p);l[v]=(u[0]+1)/2*255|0;l[v+1]=(u[1]+1)/2*255|0;l[v+2]=255*u[2]|0;l[v+3]=255}g.putImageData(k,0,0);return f},generateDataTexture:function(a,"
                << std::endl ;
            out
                << "b,c){var d=a*b,e=new Uint8Array(3*d),f=Math.floor(255*c.r),g=Math.floor(255*c.g);c=Math.floor(255*c.b);for(var h=0;h<d;h++)e[3*h]=f,e[3*h+1]=g,e[3*h+2]=c;a=new THREE.DataTexture(e,a,b,THREE.RGBFormat);a.needsUpdate=!0;return a}};"
                << std::endl ;
            out
                << "THREE.SceneUtils={createMultiMaterialObject:function(a,b){for(var c=new THREE.Object3D,d=0,e=b.length;d<e;d++)c.add(new THREE.Mesh(a,b[d]));return c},detach:function(a,b,c){a.applyMatrix(b.matrixWorld);b.remove(a);c.add(a)},attach:function(a,b,c){var d=new THREE.Matrix4;d.getInverse(c.matrixWorld);a.applyMatrix(d);b.remove(a);c.add(a)}};"
                << std::endl ;
            out
                << "THREE.FontUtils={faces:{},face:\"helvetiker\",weight:\"normal\",style:\"normal\",size:150,divisions:10,getFace:function(){try{return this.faces[this.face][this.weight][this.style]}catch(a){throw\"The font \"+this.face+\" with \"+this.weight+\" weight and \"+this.style+\" style is missing.\";}},loadFace:function(a){var b=a.familyName.toLowerCase();this.faces[b]=this.faces[b]||{};this.faces[b][a.cssFontWeight]=this.faces[b][a.cssFontWeight]||{};this.faces[b][a.cssFontWeight][a.cssFontStyle]=a;return this.faces[b][a.cssFontWeight][a.cssFontStyle]="
                << std::endl ;
            out
                << "a},drawText:function(a){var b=this.getFace(),c=this.size/b.resolution,d=0,e=String(a).split(\"\"),f=e.length,g=[];for(a=0;a<f;a++){var h=new THREE.Path,h=this.extractGlyphPoints(e[a],b,c,d,h),d=d+h.offset;g.push(h.path)}return{paths:g,offset:d/2}},extractGlyphPoints:function(a,b,c,d,e){var f=[],g,h,k,l,p,q,n,t,r,s,u,v=b.glyphs[a]||b.glyphs[\"?\"];if(v){if(v.o)for(b=v._cachedOutline||(v._cachedOutline=v.o.split(\" \")),l=b.length,a=0;a<l;)switch(k=b[a++],k){case \"m\":k=b[a++]*c+d;p=b[a++]*c;e.moveTo(k,p);"
                << std::endl ;
            out
                << "break;case \"l\":k=b[a++]*c+d;p=b[a++]*c;e.lineTo(k,p);break;case \"q\":k=b[a++]*c+d;p=b[a++]*c;t=b[a++]*c+d;r=b[a++]*c;e.quadraticCurveTo(t,r,k,p);if(g=f[f.length-1])for(q=g.x,n=g.y,g=1,h=this.divisions;g<=h;g++){var x=g/h;THREE.Shape.Utils.b2(x,q,t,k);THREE.Shape.Utils.b2(x,n,r,p)}break;case \"b\":if(k=b[a++]*c+d,p=b[a++]*c,t=b[a++]*c+d,r=b[a++]*c,s=b[a++]*c+d,u=b[a++]*c,e.bezierCurveTo(t,r,s,u,k,p),g=f[f.length-1])for(q=g.x,n=g.y,g=1,h=this.divisions;g<=h;g++)x=g/h,THREE.Shape.Utils.b3(x,q,t,s,k),THREE.Shape.Utils.b3(x,"
                << std::endl ;
            out << "n,r,u,p)}return{offset:v.ha*c,path:e}}}};" << std::endl ;
            out
                << "THREE.FontUtils.generateShapes=function(a,b){b=b||{};var c=void 0!==b.curveSegments?b.curveSegments:4,d=void 0!==b.font?b.font:\"helvetiker\",e=void 0!==b.weight?b.weight:\"normal\",f=void 0!==b.style?b.style:\"normal\";THREE.FontUtils.size=void 0!==b.size?b.size:100;THREE.FontUtils.divisions=c;THREE.FontUtils.face=d;THREE.FontUtils.weight=e;THREE.FontUtils.style=f;c=THREE.FontUtils.drawText(a).paths;d=[];e=0;for(f=c.length;e<f;e++)Array.prototype.push.apply(d,c[e].toShapes());return d};"
                << std::endl ;
            out
                << "(function(a){var b=function(a){for(var b=a.length,e=0,f=b-1,g=0;g<b;f=g++)e+=a[f].x*a[g].y-a[g].x*a[f].y;return.5*e};a.Triangulate=function(a,d){var e=a.length;if(3>e)return null;var f=[],g=[],h=[],k,l,p;if(0<b(a))for(l=0;l<e;l++)g[l]=l;else for(l=0;l<e;l++)g[l]=e-1-l;var q=2*e;for(l=e-1;2<e;){if(0>=q--){THREE.warn(\"THREE.FontUtils: Warning, unable to triangulate polygon! in Triangulate.process()\");break}k=l;e<=k&&(k=0);l=k+1;e<=l&&(l=0);p=l+1;e<=p&&(p=0);var n;a:{var t=n=void 0,r=void 0,s=void 0,"
                << std::endl ;
            out
                << "u=void 0,v=void 0,x=void 0,D=void 0,w=void 0,t=a[g[k]].x,r=a[g[k]].y,s=a[g[l]].x,u=a[g[l]].y,v=a[g[p]].x,x=a[g[p]].y;if(1E-10>(s-t)*(x-r)-(u-r)*(v-t))n=!1;else{var y=void 0,A=void 0,E=void 0,G=void 0,F=void 0,z=void 0,I=void 0,U=void 0,M=void 0,H=void 0,M=U=I=w=D=void 0,y=v-s,A=x-u,E=t-v,G=r-x,F=s-t,z=u-r;for(n=0;n<e;n++)if(D=a[g[n]].x,w=a[g[n]].y,!(D===t&&w===r||D===s&&w===u||D===v&&w===x)&&(I=D-t,U=w-r,M=D-s,H=w-u,D-=v,w-=x,M=y*H-A*M,I=F*U-z*I,U=E*w-G*D,-1E-10<=M&&-1E-10<=U&&-1E-10<=I)){n=!1;break a}n="
                << std::endl ;
            out
                << "!0}}if(n){f.push([a[g[k]],a[g[l]],a[g[p]]]);h.push([g[k],g[l],g[p]]);k=l;for(p=l+1;p<e;k++,p++)g[k]=g[p];e--;q=2*e}}return d?h:f};a.Triangulate.area=b;return a})(THREE.FontUtils);self._typeface_js={faces:THREE.FontUtils.faces,loadFace:THREE.FontUtils.loadFace};THREE.typeface_js=self._typeface_js;"
                << std::endl ;
            out
                << "THREE.Audio=function(a){THREE.Object3D.call(this);this.type=\"Audio\";this.context=a.context;this.source=this.context.createBufferSource();this.source.onended=this.onEnded.bind(this);this.gain=this.context.createGain();this.gain.connect(this.context.destination);this.panner=this.context.createPanner();this.panner.connect(this.gain);this.autoplay=!1;this.startTime=0;this.isPlaying=!1};THREE.Audio.prototype=Object.create(THREE.Object3D.prototype);THREE.Audio.prototype.constructor=THREE.Audio;"
                << std::endl ;
            out
                << "THREE.Audio.prototype.load=function(a){var b=this,c=new XMLHttpRequest;c.open(\"GET\",a,!0);c.responseType=\"arraybuffer\";c.onload=function(a){b.context.decodeAudioData(this.response,function(a){b.source.buffer=a;b.autoplay&&b.play()})};c.send();return this};"
                << std::endl ;
            out
                << "THREE.Audio.prototype.play=function(){if(!0===this.isPlaying)THREE.warn(\"THREE.Audio: Audio is already playing.\");else{var a=this.context.createBufferSource();a.buffer=this.source.buffer;a.loop=this.source.loop;a.onended=this.source.onended;a.connect(this.panner);a.start(0,this.startTime);this.isPlaying=!0;this.source=a}};THREE.Audio.prototype.pause=function(){this.source.stop();this.startTime=this.context.currentTime};THREE.Audio.prototype.stop=function(){this.source.stop();this.startTime=0};"
                << std::endl ;
            out
                << "THREE.Audio.prototype.onEnded=function(){this.isPlaying=!1};THREE.Audio.prototype.setLoop=function(a){this.source.loop=a};THREE.Audio.prototype.setRefDistance=function(a){this.panner.refDistance=a};THREE.Audio.prototype.setRolloffFactor=function(a){this.panner.rolloffFactor=a};THREE.Audio.prototype.setVolume=function(a){this.gain.gain.value=a};"
                << std::endl ;
            out
                << "THREE.Audio.prototype.updateMatrixWorld=function(){var a=new THREE.Vector3;return function(b){THREE.Object3D.prototype.updateMatrixWorld.call(this,b);a.setFromMatrixPosition(this.matrixWorld);this.panner.setPosition(a.x,a.y,a.z)}}();THREE.AudioListener=function(){THREE.Object3D.call(this);this.type=\"AudioListener\";this.context=new (window.AudioContext||window.webkitAudioContext)};THREE.AudioListener.prototype=Object.create(THREE.Object3D.prototype);THREE.AudioListener.prototype.constructor=THREE.AudioListener;"
                << std::endl ;
            out
                << "THREE.AudioListener.prototype.updateMatrixWorld=function(){var a=new THREE.Vector3,b=new THREE.Quaternion,c=new THREE.Vector3,d=new THREE.Vector3,e=new THREE.Vector3,f=new THREE.Vector3;return function(g){THREE.Object3D.prototype.updateMatrixWorld.call(this,g);g=this.context.listener;var h=this.up;this.matrixWorld.decompose(a,b,c);d.set(0,0,-1).applyQuaternion(b);e.subVectors(a,f);g.setPosition(a.x,a.y,a.z);g.setOrientation(d.x,d.y,d.z,h.x,h.y,h.z);g.setVelocity(e.x,e.y,e.z);f.copy(a)}}();"
                << std::endl ;
            out
                << "THREE.Curve=function(){};THREE.Curve.prototype.getPoint=function(a){THREE.warn(\"THREE.Curve: Warning, getPoint() not implemented!\");return null};THREE.Curve.prototype.getPointAt=function(a){a=this.getUtoTmapping(a);return this.getPoint(a)};THREE.Curve.prototype.getPoints=function(a){a||(a=5);var b,c=[];for(b=0;b<=a;b++)c.push(this.getPoint(b/a));return c};THREE.Curve.prototype.getSpacedPoints=function(a){a||(a=5);var b,c=[];for(b=0;b<=a;b++)c.push(this.getPointAt(b/a));return c};"
                << std::endl ;
            out
                << "THREE.Curve.prototype.getLength=function(){var a=this.getLengths();return a[a.length-1]};THREE.Curve.prototype.getLengths=function(a){a||(a=this.__arcLengthDivisions?this.__arcLengthDivisions:200);if(this.cacheArcLengths&&this.cacheArcLengths.length==a+1&&!this.needsUpdate)return this.cacheArcLengths;this.needsUpdate=!1;var b=[],c,d=this.getPoint(0),e,f=0;b.push(0);for(e=1;e<=a;e++)c=this.getPoint(e/a),f+=c.distanceTo(d),b.push(f),d=c;return this.cacheArcLengths=b};"
                << std::endl ;
            out
                << "THREE.Curve.prototype.updateArcLengths=function(){this.needsUpdate=!0;this.getLengths()};THREE.Curve.prototype.getUtoTmapping=function(a,b){var c=this.getLengths(),d=0,e=c.length,f;f=b?b:a*c[e-1];for(var g=0,h=e-1,k;g<=h;)if(d=Math.floor(g+(h-g)/2),k=c[d]-f,0>k)g=d+1;else if(0<k)h=d-1;else{h=d;break}d=h;if(c[d]==f)return d/(e-1);g=c[d];return c=(d+(f-g)/(c[d+1]-g))/(e-1)};THREE.Curve.prototype.getTangent=function(a){var b=a-1E-4;a+=1E-4;0>b&&(b=0);1<a&&(a=1);b=this.getPoint(b);return this.getPoint(a).clone().sub(b).normalize()};"
                << std::endl ;
            out
                << "THREE.Curve.prototype.getTangentAt=function(a){a=this.getUtoTmapping(a);return this.getTangent(a)};"
                << std::endl ;
            out
                << "THREE.Curve.Utils={tangentQuadraticBezier:function(a,b,c,d){return 2*(1-a)*(c-b)+2*a*(d-c)},tangentCubicBezier:function(a,b,c,d,e){return-3*b*(1-a)*(1-a)+3*c*(1-a)*(1-a)-6*a*c*(1-a)+6*a*d*(1-a)-3*a*a*d+3*a*a*e},tangentSpline:function(a,b,c,d,e){return 6*a*a-6*a+(3*a*a-4*a+1)+(-6*a*a+6*a)+(3*a*a-2*a)},interpolate:function(a,b,c,d,e){a=.5*(c-a);d=.5*(d-b);var f=e*e;return(2*b-2*c+a+d)*e*f+(-3*b+3*c-2*a-d)*f+a*e+b}};"
                << std::endl ;
            out
                << "THREE.Curve.create=function(a,b){a.prototype=Object.create(THREE.Curve.prototype);a.prototype.constructor=a;a.prototype.getPoint=b;return a};THREE.CurvePath=function(){this.curves=[];this.bends=[];this.autoClose=!1};THREE.CurvePath.prototype=Object.create(THREE.Curve.prototype);THREE.CurvePath.prototype.constructor=THREE.CurvePath;THREE.CurvePath.prototype.add=function(a){this.curves.push(a)};THREE.CurvePath.prototype.checkConnection=function(){};"
                << std::endl ;
            out
                << "THREE.CurvePath.prototype.closePath=function(){var a=this.curves[0].getPoint(0),b=this.curves[this.curves.length-1].getPoint(1);a.equals(b)||this.curves.push(new THREE.LineCurve(b,a))};THREE.CurvePath.prototype.getPoint=function(a){var b=a*this.getLength(),c=this.getCurveLengths();for(a=0;a<c.length;){if(c[a]>=b)return b=c[a]-b,a=this.curves[a],b=1-b/a.getLength(),a.getPointAt(b);a++}return null};THREE.CurvePath.prototype.getLength=function(){var a=this.getCurveLengths();return a[a.length-1]};"
                << std::endl ;
            out
                << "THREE.CurvePath.prototype.getCurveLengths=function(){if(this.cacheLengths&&this.cacheLengths.length==this.curves.length)return this.cacheLengths;var a=[],b=0,c,d=this.curves.length;for(c=0;c<d;c++)b+=this.curves[c].getLength(),a.push(b);return this.cacheLengths=a};"
                << std::endl ;
            out
                << "THREE.CurvePath.prototype.getBoundingBox=function(){var a=this.getPoints(),b,c,d,e,f,g;b=c=Number.NEGATIVE_INFINITY;e=f=Number.POSITIVE_INFINITY;var h,k,l,p,q=a[0]instanceof THREE.Vector3;p=q?new THREE.Vector3:new THREE.Vector2;k=0;for(l=a.length;k<l;k++)h=a[k],h.x>b?b=h.x:h.x<e&&(e=h.x),h.y>c?c=h.y:h.y<f&&(f=h.y),q&&(h.z>d?d=h.z:h.z<g&&(g=h.z)),p.add(h);a={minX:e,minY:f,maxX:b,maxY:c};q&&(a.maxZ=d,a.minZ=g);return a};"
                << std::endl ;
            out
                << "THREE.CurvePath.prototype.createPointsGeometry=function(a){a=this.getPoints(a,!0);return this.createGeometry(a)};THREE.CurvePath.prototype.createSpacedPointsGeometry=function(a){a=this.getSpacedPoints(a,!0);return this.createGeometry(a)};THREE.CurvePath.prototype.createGeometry=function(a){for(var b=new THREE.Geometry,c=0;c<a.length;c++)b.vertices.push(new THREE.Vector3(a[c].x,a[c].y,a[c].z||0));return b};THREE.CurvePath.prototype.addWrapPath=function(a){this.bends.push(a)};"
                << std::endl ;
            out
                << "THREE.CurvePath.prototype.getTransformedPoints=function(a,b){var c=this.getPoints(a),d,e;b||(b=this.bends);d=0;for(e=b.length;d<e;d++)c=this.getWrapPoints(c,b[d]);return c};THREE.CurvePath.prototype.getTransformedSpacedPoints=function(a,b){var c=this.getSpacedPoints(a),d,e;b||(b=this.bends);d=0;for(e=b.length;d<e;d++)c=this.getWrapPoints(c,b[d]);return c};"
                << std::endl ;
            out
                << "THREE.CurvePath.prototype.getWrapPoints=function(a,b){var c=this.getBoundingBox(),d,e,f,g,h,k;d=0;for(e=a.length;d<e;d++)f=a[d],g=f.x,h=f.y,k=g/c.maxX,k=b.getUtoTmapping(k,g),g=b.getPoint(k),k=b.getTangent(k),k.set(-k.y,k.x).multiplyScalar(h),f.x=g.x+k.x,f.y=g.y+k.y;return a};THREE.Gyroscope=function(){THREE.Object3D.call(this)};THREE.Gyroscope.prototype=Object.create(THREE.Object3D.prototype);THREE.Gyroscope.prototype.constructor=THREE.Gyroscope;"
                << std::endl ;
            out
                << "THREE.Gyroscope.prototype.updateMatrixWorld=function(){var a=new THREE.Vector3,b=new THREE.Quaternion,c=new THREE.Vector3,d=new THREE.Vector3,e=new THREE.Quaternion,f=new THREE.Vector3;return function(g){this.matrixAutoUpdate&&this.updateMatrix();if(this.matrixWorldNeedsUpdate||g)this.parent?(this.matrixWorld.multiplyMatrices(this.parent.matrixWorld,this.matrix),this.matrixWorld.decompose(d,e,f),this.matrix.decompose(a,b,c),this.matrixWorld.compose(d,b,f)):this.matrixWorld.copy(this.matrix),this.matrixWorldNeedsUpdate="
                << std::endl ;
            out
                << "!1,g=!0;for(var h=0,k=this.children.length;h<k;h++)this.children[h].updateMatrixWorld(g)}}();THREE.Path=function(a){THREE.CurvePath.call(this);this.actions=[];a&&this.fromPoints(a)};THREE.Path.prototype=Object.create(THREE.CurvePath.prototype);THREE.Path.prototype.constructor=THREE.Path;THREE.PathActions={MOVE_TO:\"moveTo\",LINE_TO:\"lineTo\",QUADRATIC_CURVE_TO:\"quadraticCurveTo\",BEZIER_CURVE_TO:\"bezierCurveTo\",CSPLINE_THRU:\"splineThru\",ARC:\"arc\",ELLIPSE:\"ellipse\"};"
                << std::endl ;
            out
                << "THREE.Path.prototype.fromPoints=function(a){this.moveTo(a[0].x,a[0].y);for(var b=1,c=a.length;b<c;b++)this.lineTo(a[b].x,a[b].y)};THREE.Path.prototype.moveTo=function(a,b){var c=Array.prototype.slice.call(arguments);this.actions.push({action:THREE.PathActions.MOVE_TO,args:c})};"
                << std::endl ;
            out
                << "THREE.Path.prototype.lineTo=function(a,b){var c=Array.prototype.slice.call(arguments),d=this.actions[this.actions.length-1].args,d=new THREE.LineCurve(new THREE.Vector2(d[d.length-2],d[d.length-1]),new THREE.Vector2(a,b));this.curves.push(d);this.actions.push({action:THREE.PathActions.LINE_TO,args:c})};"
                << std::endl ;
            out
                << "THREE.Path.prototype.quadraticCurveTo=function(a,b,c,d){var e=Array.prototype.slice.call(arguments),f=this.actions[this.actions.length-1].args,f=new THREE.QuadraticBezierCurve(new THREE.Vector2(f[f.length-2],f[f.length-1]),new THREE.Vector2(a,b),new THREE.Vector2(c,d));this.curves.push(f);this.actions.push({action:THREE.PathActions.QUADRATIC_CURVE_TO,args:e})};"
                << std::endl ;
            out
                << "THREE.Path.prototype.bezierCurveTo=function(a,b,c,d,e,f){var g=Array.prototype.slice.call(arguments),h=this.actions[this.actions.length-1].args,h=new THREE.CubicBezierCurve(new THREE.Vector2(h[h.length-2],h[h.length-1]),new THREE.Vector2(a,b),new THREE.Vector2(c,d),new THREE.Vector2(e,f));this.curves.push(h);this.actions.push({action:THREE.PathActions.BEZIER_CURVE_TO,args:g})};"
                << std::endl ;
            out
                << "THREE.Path.prototype.splineThru=function(a){var b=Array.prototype.slice.call(arguments),c=this.actions[this.actions.length-1].args,c=[new THREE.Vector2(c[c.length-2],c[c.length-1])];Array.prototype.push.apply(c,a);c=new THREE.SplineCurve(c);this.curves.push(c);this.actions.push({action:THREE.PathActions.CSPLINE_THRU,args:b})};THREE.Path.prototype.arc=function(a,b,c,d,e,f){var g=this.actions[this.actions.length-1].args;this.absarc(a+g[g.length-2],b+g[g.length-1],c,d,e,f)};"
                << std::endl ;
            out
                << "THREE.Path.prototype.absarc=function(a,b,c,d,e,f){this.absellipse(a,b,c,c,d,e,f)};THREE.Path.prototype.ellipse=function(a,b,c,d,e,f,g){var h=this.actions[this.actions.length-1].args;this.absellipse(a+h[h.length-2],b+h[h.length-1],c,d,e,f,g)};THREE.Path.prototype.absellipse=function(a,b,c,d,e,f,g){var h=Array.prototype.slice.call(arguments),k=new THREE.EllipseCurve(a,b,c,d,e,f,g);this.curves.push(k);k=k.getPoint(1);h.push(k.x);h.push(k.y);this.actions.push({action:THREE.PathActions.ELLIPSE,args:h})};"
                << std::endl ;
            out
                << "THREE.Path.prototype.getSpacedPoints=function(a,b){a||(a=40);for(var c=[],d=0;d<a;d++)c.push(this.getPoint(d/a));return c};"
                << std::endl ;
            out
                << "THREE.Path.prototype.getPoints=function(a,b){if(this.useSpacedPoints)return console.log(\"tata\"),this.getSpacedPoints(a,b);a=a||12;var c=[],d,e,f,g,h,k,l,p,q,n,t,r,s;d=0;for(e=this.actions.length;d<e;d++)switch(f=this.actions[d],g=f.action,f=f.args,g){case THREE.PathActions.MOVE_TO:c.push(new THREE.Vector2(f[0],f[1]));break;case THREE.PathActions.LINE_TO:c.push(new THREE.Vector2(f[0],f[1]));break;case THREE.PathActions.QUADRATIC_CURVE_TO:h=f[2];k=f[3];q=f[0];n=f[1];0<c.length?(g=c[c.length-1],t=g.x,"
                << std::endl ;
            out
                << "r=g.y):(g=this.actions[d-1].args,t=g[g.length-2],r=g[g.length-1]);for(f=1;f<=a;f++)s=f/a,g=THREE.Shape.Utils.b2(s,t,q,h),s=THREE.Shape.Utils.b2(s,r,n,k),c.push(new THREE.Vector2(g,s));break;case THREE.PathActions.BEZIER_CURVE_TO:h=f[4];k=f[5];q=f[0];n=f[1];l=f[2];p=f[3];0<c.length?(g=c[c.length-1],t=g.x,r=g.y):(g=this.actions[d-1].args,t=g[g.length-2],r=g[g.length-1]);for(f=1;f<=a;f++)s=f/a,g=THREE.Shape.Utils.b3(s,t,q,l,h),s=THREE.Shape.Utils.b3(s,r,n,p,k),c.push(new THREE.Vector2(g,s));break;case THREE.PathActions.CSPLINE_THRU:g="
                << std::endl ;
            out
                << "this.actions[d-1].args;s=[new THREE.Vector2(g[g.length-2],g[g.length-1])];g=a*f[0].length;s=s.concat(f[0]);s=new THREE.SplineCurve(s);for(f=1;f<=g;f++)c.push(s.getPointAt(f/g));break;case THREE.PathActions.ARC:h=f[0];k=f[1];n=f[2];l=f[3];g=f[4];q=!!f[5];t=g-l;r=2*a;for(f=1;f<=r;f++)s=f/r,q||(s=1-s),s=l+s*t,g=h+n*Math.cos(s),s=k+n*Math.sin(s),c.push(new THREE.Vector2(g,s));break;case THREE.PathActions.ELLIPSE:for(h=f[0],k=f[1],n=f[2],p=f[3],l=f[4],g=f[5],q=!!f[6],t=g-l,r=2*a,f=1;f<=r;f++)s=f/r,q||"
                << std::endl ;
            out
                << "(s=1-s),s=l+s*t,g=h+n*Math.cos(s),s=k+p*Math.sin(s),c.push(new THREE.Vector2(g,s))}d=c[c.length-1];1E-10>Math.abs(d.x-c[0].x)&&1E-10>Math.abs(d.y-c[0].y)&&c.splice(c.length-1,1);b&&c.push(c[0]);return c};"
                << std::endl ;
            out
                << "THREE.Path.prototype.toShapes=function(a,b){function c(a){for(var b=[],c=0,d=a.length;c<d;c++){var e=a[c],f=new THREE.Shape;f.actions=e.actions;f.curves=e.curves;b.push(f)}return b}function d(a,b){for(var c=b.length,d=!1,e=c-1,f=0;f<c;e=f++){var g=b[e],h=b[f],k=h.x-g.x,n=h.y-g.y;if(1E-10<Math.abs(n)){if(0>n&&(g=b[f],k=-k,h=b[e],n=-n),!(a.y<g.y||a.y>h.y))if(a.y==g.y){if(a.x==g.x)return!0}else{e=n*(a.x-g.x)-k*(a.y-g.y);if(0==e)return!0;0>e||(d=!d)}}else if(a.y==g.y&&(h.x<=a.x&&a.x<=g.x||g.x<=a.x&&a.x<="
                << std::endl ;
            out
                << "h.x))return!0}return d}var e=function(a){var b,c,d,e,f=[],g=new THREE.Path;b=0;for(c=a.length;b<c;b++)d=a[b],e=d.args,d=d.action,d==THREE.PathActions.MOVE_TO&&0!=g.actions.length&&(f.push(g),g=new THREE.Path),g[d].apply(g,e);0!=g.actions.length&&f.push(g);return f}(this.actions);if(0==e.length)return[];if(!0===b)return c(e);var f,g,h,k=[];if(1==e.length)return g=e[0],h=new THREE.Shape,h.actions=g.actions,h.curves=g.curves,k.push(h),k;var l=!THREE.Shape.Utils.isClockWise(e[0].getPoints()),l=a?!l:l;"
                << std::endl ;
            out
                << "h=[];var p=[],q=[],n=0,t;p[n]=void 0;q[n]=[];var r,s;r=0;for(s=e.length;r<s;r++)g=e[r],t=g.getPoints(),f=THREE.Shape.Utils.isClockWise(t),(f=a?!f:f)?(!l&&p[n]&&n++,p[n]={s:new THREE.Shape,p:t},p[n].s.actions=g.actions,p[n].s.curves=g.curves,l&&n++,q[n]=[]):q[n].push({h:g,p:t[0]});if(!p[0])return c(e);if(1<p.length){r=!1;s=[];g=0;for(e=p.length;g<e;g++)h[g]=[];g=0;for(e=p.length;g<e;g++)for(f=q[g],l=0;l<f.length;l++){n=f[l];t=!0;for(var u=0;u<p.length;u++)d(n.p,p[u].p)&&(g!=u&&s.push({froms:g,tos:u,"
                << std::endl ;
            out
                << "hole:l}),t?(t=!1,h[u].push(n)):r=!0);t&&h[g].push(n)}0<s.length&&(r||(q=h))}r=0;for(s=p.length;r<s;r++)for(h=p[r].s,k.push(h),g=q[r],e=0,f=g.length;e<f;e++)h.holes.push(g[e].h);return k};THREE.Shape=function(){THREE.Path.apply(this,arguments);this.holes=[]};THREE.Shape.prototype=Object.create(THREE.Path.prototype);THREE.Shape.prototype.constructor=THREE.Shape;THREE.Shape.prototype.extrude=function(a){return new THREE.ExtrudeGeometry(this,a)};"
                << std::endl ;
            out
                << "THREE.Shape.prototype.makeGeometry=function(a){return new THREE.ShapeGeometry(this,a)};THREE.Shape.prototype.getPointsHoles=function(a){var b,c=this.holes.length,d=[];for(b=0;b<c;b++)d[b]=this.holes[b].getTransformedPoints(a,this.bends);return d};THREE.Shape.prototype.getSpacedPointsHoles=function(a){var b,c=this.holes.length,d=[];for(b=0;b<c;b++)d[b]=this.holes[b].getTransformedSpacedPoints(a,this.bends);return d};"
                << std::endl ;
            out
                << "THREE.Shape.prototype.extractAllPoints=function(a){return{shape:this.getTransformedPoints(a),holes:this.getPointsHoles(a)}};THREE.Shape.prototype.extractPoints=function(a){return this.useSpacedPoints?this.extractAllSpacedPoints(a):this.extractAllPoints(a)};THREE.Shape.prototype.extractAllSpacedPoints=function(a){return{shape:this.getTransformedSpacedPoints(a),holes:this.getSpacedPointsHoles(a)}};"
                << std::endl ;
            out
                << "THREE.Shape.Utils={triangulateShape:function(a,b){function c(a,b,c){return a.x!=b.x?a.x<b.x?a.x<=c.x&&c.x<=b.x:b.x<=c.x&&c.x<=a.x:a.y<b.y?a.y<=c.y&&c.y<=b.y:b.y<=c.y&&c.y<=a.y}function d(a,b,d,e,f){var g=b.x-a.x,h=b.y-a.y,k=e.x-d.x,l=e.y-d.y,p=a.x-d.x,q=a.y-d.y,E=h*k-g*l,G=h*p-g*q;if(1E-10<Math.abs(E)){if(0<E){if(0>G||G>E)return[];k=l*p-k*q;if(0>k||k>E)return[]}else{if(0<G||G<E)return[];k=l*p-k*q;if(0<k||k<E)return[]}if(0==k)return!f||0!=G&&G!=E?[a]:[];if(k==E)return!f||0!=G&&G!=E?[b]:[];if(0==G)return[d];"
                << std::endl ;
            out
                << "if(G==E)return[e];f=k/E;return[{x:a.x+f*g,y:a.y+f*h}]}if(0!=G||l*p!=k*q)return[];h=0==g&&0==h;k=0==k&&0==l;if(h&&k)return a.x!=d.x||a.y!=d.y?[]:[a];if(h)return c(d,e,a)?[a]:[];if(k)return c(a,b,d)?[d]:[];0!=g?(a.x<b.x?(g=a,k=a.x,h=b,a=b.x):(g=b,k=b.x,h=a,a=a.x),d.x<e.x?(b=d,E=d.x,l=e,d=e.x):(b=e,E=e.x,l=d,d=d.x)):(a.y<b.y?(g=a,k=a.y,h=b,a=b.y):(g=b,k=b.y,h=a,a=a.y),d.y<e.y?(b=d,E=d.y,l=e,d=e.y):(b=e,E=e.y,l=d,d=d.y));return k<=E?a<E?[]:a==E?f?[]:[b]:a<=d?[b,h]:[b,l]:k>d?[]:k==d?f?[]:[g]:a<=d?[g,h]:"
                << std::endl ;
            out
                << "[g,l]}function e(a,b,c,d){var e=b.x-a.x,f=b.y-a.y;b=c.x-a.x;c=c.y-a.y;var g=d.x-a.x;d=d.y-a.y;a=e*c-f*b;e=e*d-f*g;return 1E-10<Math.abs(a)?(b=g*c-d*b,0<a?0<=e&&0<=b:0<=e||0<=b):0<e}var f,g,h,k,l,p={};h=a.concat();f=0;for(g=b.length;f<g;f++)Array.prototype.push.apply(h,b[f]);f=0;for(g=h.length;f<g;f++)l=h[f].x+\":\"+h[f].y,void 0!==p[l]&&THREE.warn(\"THREE.Shape: Duplicate point\",l),p[l]=f;f=function(a,b){function c(a,b){var d=h.length-1,f=a-1;0>f&&(f=d);var g=a+1;g>d&&(g=0);d=e(h[a],h[f],h[g],k[b]);"
                << std::endl ;
            out
                << "if(!d)return!1;d=k.length-1;f=b-1;0>f&&(f=d);g=b+1;g>d&&(g=0);return(d=e(k[b],k[f],k[g],h[a]))?!0:!1}function f(a,b){var c,e;for(c=0;c<h.length;c++)if(e=c+1,e%=h.length,e=d(a,b,h[c],h[e],!0),0<e.length)return!0;return!1}function g(a,c){var e,f,h,k;for(e=0;e<l.length;e++)for(f=b[l[e]],h=0;h<f.length;h++)if(k=h+1,k%=f.length,k=d(a,c,f[h],f[k],!0),0<k.length)return!0;return!1}var h=a.concat(),k,l=[],p,q,A,E,G,F=[],z,I,U,M=0;for(p=b.length;M<p;M++)l.push(M);z=0;for(var H=2*l.length;0<l.length;){H--;if(0>"
                << std::endl ;
            out
                << "H){console.log(\"Infinite Loop! Holes left:\"+l.length+\", Probably Hole outside Shape!\");break}for(q=z;q<h.length;q++){A=h[q];p=-1;for(M=0;M<l.length;M++)if(E=l[M],G=A.x+\":\"+A.y+\":\"+E,void 0===F[G]){k=b[E];for(I=0;I<k.length;I++)if(E=k[I],c(q,I)&&!f(A,E)&&!g(A,E)){p=I;l.splice(M,1);z=h.slice(0,q+1);E=h.slice(q);I=k.slice(p);U=k.slice(0,p+1);h=z.concat(I).concat(U).concat(E);z=q;break}if(0<=p)break;F[G]=!0}if(0<=p)break}}return h}(a,b);var q=THREE.FontUtils.Triangulate(f,!1);f=0;for(g=q.length;f<g;f++)for(k="
                << std::endl ;
            out
                << "q[f],h=0;3>h;h++)l=k[h].x+\":\"+k[h].y,l=p[l],void 0!==l&&(k[h]=l);return q.concat()},isClockWise:function(a){return 0>THREE.FontUtils.Triangulate.area(a)},b2p0:function(a,b){var c=1-a;return c*c*b},b2p1:function(a,b){return 2*(1-a)*a*b},b2p2:function(a,b){return a*a*b},b2:function(a,b,c,d){return this.b2p0(a,b)+this.b2p1(a,c)+this.b2p2(a,d)},b3p0:function(a,b){var c=1-a;return c*c*c*b},b3p1:function(a,b){var c=1-a;return 3*c*c*a*b},b3p2:function(a,b){return 3*(1-a)*a*a*b},b3p3:function(a,b){return a*"
                << std::endl ;
            out
                << "a*a*b},b3:function(a,b,c,d,e){return this.b3p0(a,b)+this.b3p1(a,c)+this.b3p2(a,d)+this.b3p3(a,e)}};THREE.LineCurve=function(a,b){this.v1=a;this.v2=b};THREE.LineCurve.prototype=Object.create(THREE.Curve.prototype);THREE.LineCurve.prototype.constructor=THREE.LineCurve;THREE.LineCurve.prototype.getPoint=function(a){var b=this.v2.clone().sub(this.v1);b.multiplyScalar(a).add(this.v1);return b};THREE.LineCurve.prototype.getPointAt=function(a){return this.getPoint(a)};"
                << std::endl ;
            out
                << "THREE.LineCurve.prototype.getTangent=function(a){return this.v2.clone().sub(this.v1).normalize()};THREE.QuadraticBezierCurve=function(a,b,c){this.v0=a;this.v1=b;this.v2=c};THREE.QuadraticBezierCurve.prototype=Object.create(THREE.Curve.prototype);THREE.QuadraticBezierCurve.prototype.constructor=THREE.QuadraticBezierCurve;"
                << std::endl ;
            out
                << "THREE.QuadraticBezierCurve.prototype.getPoint=function(a){var b=new THREE.Vector2;b.x=THREE.Shape.Utils.b2(a,this.v0.x,this.v1.x,this.v2.x);b.y=THREE.Shape.Utils.b2(a,this.v0.y,this.v1.y,this.v2.y);return b};THREE.QuadraticBezierCurve.prototype.getTangent=function(a){var b=new THREE.Vector2;b.x=THREE.Curve.Utils.tangentQuadraticBezier(a,this.v0.x,this.v1.x,this.v2.x);b.y=THREE.Curve.Utils.tangentQuadraticBezier(a,this.v0.y,this.v1.y,this.v2.y);return b.normalize()};"
                << std::endl ;
            out
                << "THREE.CubicBezierCurve=function(a,b,c,d){this.v0=a;this.v1=b;this.v2=c;this.v3=d};THREE.CubicBezierCurve.prototype=Object.create(THREE.Curve.prototype);THREE.CubicBezierCurve.prototype.constructor=THREE.CubicBezierCurve;THREE.CubicBezierCurve.prototype.getPoint=function(a){var b;b=THREE.Shape.Utils.b3(a,this.v0.x,this.v1.x,this.v2.x,this.v3.x);a=THREE.Shape.Utils.b3(a,this.v0.y,this.v1.y,this.v2.y,this.v3.y);return new THREE.Vector2(b,a)};"
                << std::endl ;
            out
                << "THREE.CubicBezierCurve.prototype.getTangent=function(a){var b;b=THREE.Curve.Utils.tangentCubicBezier(a,this.v0.x,this.v1.x,this.v2.x,this.v3.x);a=THREE.Curve.Utils.tangentCubicBezier(a,this.v0.y,this.v1.y,this.v2.y,this.v3.y);b=new THREE.Vector2(b,a);b.normalize();return b};THREE.SplineCurve=function(a){this.points=void 0==a?[]:a};THREE.SplineCurve.prototype=Object.create(THREE.Curve.prototype);THREE.SplineCurve.prototype.constructor=THREE.SplineCurve;"
                << std::endl ;
            out
                << "THREE.SplineCurve.prototype.getPoint=function(a){var b=this.points;a*=b.length-1;var c=Math.floor(a);a-=c;var d=b[0==c?c:c-1],e=b[c],f=b[c>b.length-2?b.length-1:c+1],b=b[c>b.length-3?b.length-1:c+2],c=new THREE.Vector2;c.x=THREE.Curve.Utils.interpolate(d.x,e.x,f.x,b.x,a);c.y=THREE.Curve.Utils.interpolate(d.y,e.y,f.y,b.y,a);return c};THREE.EllipseCurve=function(a,b,c,d,e,f,g){this.aX=a;this.aY=b;this.xRadius=c;this.yRadius=d;this.aStartAngle=e;this.aEndAngle=f;this.aClockwise=g};"
                << std::endl ;
            out
                << "THREE.EllipseCurve.prototype=Object.create(THREE.Curve.prototype);THREE.EllipseCurve.prototype.constructor=THREE.EllipseCurve;THREE.EllipseCurve.prototype.getPoint=function(a){var b=this.aEndAngle-this.aStartAngle;0>b&&(b+=2*Math.PI);b>2*Math.PI&&(b-=2*Math.PI);a=!0===this.aClockwise?this.aEndAngle+(1-a)*(2*Math.PI-b):this.aStartAngle+a*b;b=new THREE.Vector2;b.x=this.aX+this.xRadius*Math.cos(a);b.y=this.aY+this.yRadius*Math.sin(a);return b};"
                << std::endl ;
            out
                << "THREE.ArcCurve=function(a,b,c,d,e,f){THREE.EllipseCurve.call(this,a,b,c,c,d,e,f)};THREE.ArcCurve.prototype=Object.create(THREE.EllipseCurve.prototype);THREE.ArcCurve.prototype.constructor=THREE.ArcCurve;THREE.LineCurve3=THREE.Curve.create(function(a,b){this.v1=a;this.v2=b},function(a){var b=new THREE.Vector3;b.subVectors(this.v2,this.v1);b.multiplyScalar(a);b.add(this.v1);return b});"
                << std::endl ;
            out
                << "THREE.QuadraticBezierCurve3=THREE.Curve.create(function(a,b,c){this.v0=a;this.v1=b;this.v2=c},function(a){var b=new THREE.Vector3;b.x=THREE.Shape.Utils.b2(a,this.v0.x,this.v1.x,this.v2.x);b.y=THREE.Shape.Utils.b2(a,this.v0.y,this.v1.y,this.v2.y);b.z=THREE.Shape.Utils.b2(a,this.v0.z,this.v1.z,this.v2.z);return b});"
                << std::endl ;
            out
                << "THREE.CubicBezierCurve3=THREE.Curve.create(function(a,b,c,d){this.v0=a;this.v1=b;this.v2=c;this.v3=d},function(a){var b=new THREE.Vector3;b.x=THREE.Shape.Utils.b3(a,this.v0.x,this.v1.x,this.v2.x,this.v3.x);b.y=THREE.Shape.Utils.b3(a,this.v0.y,this.v1.y,this.v2.y,this.v3.y);b.z=THREE.Shape.Utils.b3(a,this.v0.z,this.v1.z,this.v2.z,this.v3.z);return b});"
                << std::endl ;
            out
                << "THREE.SplineCurve3=THREE.Curve.create(function(a){this.points=void 0==a?[]:a},function(a){var b=this.points;a*=b.length-1;var c=Math.floor(a);a-=c;var d=b[0==c?c:c-1],e=b[c],f=b[c>b.length-2?b.length-1:c+1],b=b[c>b.length-3?b.length-1:c+2],c=new THREE.Vector3;c.x=THREE.Curve.Utils.interpolate(d.x,e.x,f.x,b.x,a);c.y=THREE.Curve.Utils.interpolate(d.y,e.y,f.y,b.y,a);c.z=THREE.Curve.Utils.interpolate(d.z,e.z,f.z,b.z,a);return c});"
                << std::endl ;
            out
                << "THREE.ClosedSplineCurve3=THREE.Curve.create(function(a){this.points=void 0==a?[]:a},function(a){var b=this.points;a*=b.length-0;var c=Math.floor(a);a-=c;var c=c+(0<c?0:(Math.floor(Math.abs(c)/b.length)+1)*b.length),d=b[(c-1)%b.length],e=b[c%b.length],f=b[(c+1)%b.length],b=b[(c+2)%b.length],c=new THREE.Vector3;c.x=THREE.Curve.Utils.interpolate(d.x,e.x,f.x,b.x,a);c.y=THREE.Curve.Utils.interpolate(d.y,e.y,f.y,b.y,a);c.z=THREE.Curve.Utils.interpolate(d.z,e.z,f.z,b.z,a);return c});"
                << std::endl ;
            out
                << "THREE.AnimationHandler={LINEAR:0,CATMULLROM:1,CATMULLROM_FORWARD:2,add:function(){THREE.warn(\"THREE.AnimationHandler.add() has been deprecated.\")},get:function(){THREE.warn(\"THREE.AnimationHandler.get() has been deprecated.\")},remove:function(){THREE.warn(\"THREE.AnimationHandler.remove() has been deprecated.\")},animations:[],init:function(a){if(!0===a.initialized)return a;for(var b=0;b<a.hierarchy.length;b++){for(var c=0;c<a.hierarchy[b].keys.length;c++)if(0>a.hierarchy[b].keys[c].time&&(a.hierarchy[b].keys[c].time="
                << std::endl ;
            out
                << "0),void 0!==a.hierarchy[b].keys[c].rot&&!(a.hierarchy[b].keys[c].rot instanceof THREE.Quaternion)){var d=a.hierarchy[b].keys[c].rot;a.hierarchy[b].keys[c].rot=(new THREE.Quaternion).fromArray(d)}if(a.hierarchy[b].keys.length&&void 0!==a.hierarchy[b].keys[0].morphTargets){d={};for(c=0;c<a.hierarchy[b].keys.length;c++)for(var e=0;e<a.hierarchy[b].keys[c].morphTargets.length;e++){var f=a.hierarchy[b].keys[c].morphTargets[e];d[f]=-1}a.hierarchy[b].usedMorphTargets=d;for(c=0;c<a.hierarchy[b].keys.length;c++){var g="
                << std::endl ;
            out
                << "{};for(f in d){for(e=0;e<a.hierarchy[b].keys[c].morphTargets.length;e++)if(a.hierarchy[b].keys[c].morphTargets[e]===f){g[f]=a.hierarchy[b].keys[c].morphTargetsInfluences[e];break}e===a.hierarchy[b].keys[c].morphTargets.length&&(g[f]=0)}a.hierarchy[b].keys[c].morphTargetsInfluences=g}}for(c=1;c<a.hierarchy[b].keys.length;c++)a.hierarchy[b].keys[c].time===a.hierarchy[b].keys[c-1].time&&(a.hierarchy[b].keys.splice(c,1),c--);for(c=0;c<a.hierarchy[b].keys.length;c++)a.hierarchy[b].keys[c].index=c}a.initialized="
                << std::endl ;
            out
                << "!0;return a},parse:function(a){var b=function(a,c){c.push(a);for(var d=0;d<a.children.length;d++)b(a.children[d],c)},c=[];if(a instanceof THREE.SkinnedMesh)for(var d=0;d<a.skeleton.bones.length;d++)c.push(a.skeleton.bones[d]);else b(a,c);return c},play:function(a){-1===this.animations.indexOf(a)&&this.animations.push(a)},stop:function(a){a=this.animations.indexOf(a);-1!==a&&this.animations.splice(a,1)},update:function(a){for(var b=0;b<this.animations.length;b++)this.animations[b].resetBlendWeights();"
                << std::endl ;
            out
                << "for(b=0;b<this.animations.length;b++)this.animations[b].update(a)}};THREE.Animation=function(a,b){this.root=a;this.data=THREE.AnimationHandler.init(b);this.hierarchy=THREE.AnimationHandler.parse(a);this.currentTime=0;this.timeScale=1;this.isPlaying=!1;this.loop=!0;this.weight=0;this.interpolationType=THREE.AnimationHandler.LINEAR};"
                << std::endl ;
            out
                << "THREE.Animation.prototype={constructor:THREE.Animation,keyTypes:[\"pos\",\"rot\",\"scl\"],play:function(a,b){this.currentTime=void 0!==a?a:0;this.weight=void 0!==b?b:1;this.isPlaying=!0;this.reset();THREE.AnimationHandler.play(this)},stop:function(){this.isPlaying=!1;THREE.AnimationHandler.stop(this)},reset:function(){for(var a=0,b=this.hierarchy.length;a<b;a++){var c=this.hierarchy[a];void 0===c.animationCache&&(c.animationCache={animations:{},blending:{positionWeight:0,quaternionWeight:0,scaleWeight:0}});"
                << std::endl ;
            out
                << "var d=this.data.name,e=c.animationCache.animations,f=e[d];void 0===f&&(f={prevKey:{pos:0,rot:0,scl:0},nextKey:{pos:0,rot:0,scl:0},originalMatrix:c.matrix},e[d]=f);for(c=0;3>c;c++){for(var d=this.keyTypes[c],e=this.data.hierarchy[a].keys[0],g=this.getNextKeyWith(d,a,1);g.time<this.currentTime&&g.index>e.index;)e=g,g=this.getNextKeyWith(d,a,g.index+1);f.prevKey[d]=e;f.nextKey[d]=g}}},resetBlendWeights:function(){for(var a=0,b=this.hierarchy.length;a<b;a++){var c=this.hierarchy[a].animationCache;void 0!=="
                << std::endl ;
            out
                << "c&&(c=c.blending,c.positionWeight=0,c.quaternionWeight=0,c.scaleWeight=0)}},update:function(){var a=[],b=new THREE.Vector3,c=new THREE.Vector3,d=new THREE.Quaternion,e=function(a,b){var c=[],d=[],e,q,n,t,r,s;e=(a.length-1)*b;q=Math.floor(e);e-=q;c[0]=0===q?q:q-1;c[1]=q;c[2]=q>a.length-2?q:q+1;c[3]=q>a.length-3?q:q+2;q=a[c[0]];t=a[c[1]];r=a[c[2]];s=a[c[3]];c=e*e;n=e*c;d[0]=f(q[0],t[0],r[0],s[0],e,c,n);d[1]=f(q[1],t[1],r[1],s[1],e,c,n);d[2]=f(q[2],t[2],r[2],s[2],e,c,n);return d},f=function(a,b,c,d,"
                << std::endl ;
            out
                << "e,f,n){a=.5*(c-a);d=.5*(d-b);return(2*(b-c)+a+d)*n+(-3*(b-c)-2*a-d)*f+a*e+b};return function(f){if(!1!==this.isPlaying&&(this.currentTime+=f*this.timeScale,0!==this.weight)){f=this.data.length;if(this.currentTime>f||0>this.currentTime)this.loop?(this.currentTime%=f,0>this.currentTime&&(this.currentTime+=f),this.reset()):this.stop();f=0;for(var h=this.hierarchy.length;f<h;f++)for(var k=this.hierarchy[f],l=k.animationCache.animations[this.data.name],p=k.animationCache.blending,q=0;3>q;q++){var n=this.keyTypes[q],"
                << std::endl ;
            out
                << "t=l.prevKey[n],r=l.nextKey[n];if(0<this.timeScale&&r.time<=this.currentTime||0>this.timeScale&&t.time>=this.currentTime){t=this.data.hierarchy[f].keys[0];for(r=this.getNextKeyWith(n,f,1);r.time<this.currentTime&&r.index>t.index;)t=r,r=this.getNextKeyWith(n,f,r.index+1);l.prevKey[n]=t;l.nextKey[n]=r}var s=(this.currentTime-t.time)/(r.time-t.time),u=t[n],v=r[n];0>s&&(s=0);1<s&&(s=1);if(\"pos\"===n)if(this.interpolationType===THREE.AnimationHandler.LINEAR)c.x=u[0]+(v[0]-u[0])*s,c.y=u[1]+(v[1]-u[1])*s,"
                << std::endl ;
            out
                << "c.z=u[2]+(v[2]-u[2])*s,t=this.weight/(this.weight+p.positionWeight),k.position.lerp(c,t),p.positionWeight+=this.weight;else{if(this.interpolationType===THREE.AnimationHandler.CATMULLROM||this.interpolationType===THREE.AnimationHandler.CATMULLROM_FORWARD)a[0]=this.getPrevKeyWith(\"pos\",f,t.index-1).pos,a[1]=u,a[2]=v,a[3]=this.getNextKeyWith(\"pos\",f,r.index+1).pos,s=.33*s+.33,r=e(a,s),t=this.weight/(this.weight+p.positionWeight),p.positionWeight+=this.weight,n=k.position,n.x+=(r[0]-n.x)*t,n.y+=(r[1]-"
                << std::endl ;
            out
                << "n.y)*t,n.z+=(r[2]-n.z)*t,this.interpolationType===THREE.AnimationHandler.CATMULLROM_FORWARD&&(s=e(a,1.01*s),b.set(s[0],s[1],s[2]),b.sub(n),b.y=0,b.normalize(),s=Math.atan2(b.x,b.z),k.rotation.set(0,s,0))}else\"rot\"===n?(THREE.Quaternion.slerp(u,v,d,s),0===p.quaternionWeight?(k.quaternion.copy(d),p.quaternionWeight=this.weight):(t=this.weight/(this.weight+p.quaternionWeight),THREE.Quaternion.slerp(k.quaternion,d,k.quaternion,t),p.quaternionWeight+=this.weight)):\"scl\"===n&&(c.x=u[0]+(v[0]-u[0])*s,c.y="
                << std::endl ;
            out
                << "u[1]+(v[1]-u[1])*s,c.z=u[2]+(v[2]-u[2])*s,t=this.weight/(this.weight+p.scaleWeight),k.scale.lerp(c,t),p.scaleWeight+=this.weight)}return!0}}}(),getNextKeyWith:function(a,b,c){var d=this.data.hierarchy[b].keys;for(c=this.interpolationType===THREE.AnimationHandler.CATMULLROM||this.interpolationType===THREE.AnimationHandler.CATMULLROM_FORWARD?c<d.length-1?c:d.length-1:c%d.length;c<d.length;c++)if(void 0!==d[c][a])return d[c];return this.data.hierarchy[b].keys[0]},getPrevKeyWith:function(a,b,c){var d="
                << std::endl ;
            out
                << "this.data.hierarchy[b].keys;for(c=this.interpolationType===THREE.AnimationHandler.CATMULLROM||this.interpolationType===THREE.AnimationHandler.CATMULLROM_FORWARD?0<c?c:0:0<=c?c:c+d.length;0<=c;c--)if(void 0!==d[c][a])return d[c];return this.data.hierarchy[b].keys[d.length-1]}};"
                << std::endl ;
            out
                << "THREE.KeyFrameAnimation=function(a){this.root=a.node;this.data=THREE.AnimationHandler.init(a);this.hierarchy=THREE.AnimationHandler.parse(this.root);this.currentTime=0;this.timeScale=.001;this.isPlaying=!1;this.loop=this.isPaused=!0;a=0;for(var b=this.hierarchy.length;a<b;a++){var c=this.data.hierarchy[a].sids,d=this.hierarchy[a];if(this.data.hierarchy[a].keys.length&&c){for(var e=0;e<c.length;e++){var f=c[e],g=this.getNextKeyWith(f,a,0);g&&g.apply(f)}d.matrixAutoUpdate=!1;this.data.hierarchy[a].node.updateMatrix();"
                << std::endl ;
            out << "d.matrixWorldNeedsUpdate=!0}}};" << std::endl ;
            out
                << "THREE.KeyFrameAnimation.prototype={constructor:THREE.KeyFrameAnimation,play:function(a){this.currentTime=void 0!==a?a:0;if(!1===this.isPlaying){this.isPlaying=!0;var b=this.hierarchy.length,c,d;for(a=0;a<b;a++)c=this.hierarchy[a],d=this.data.hierarchy[a],void 0===d.animationCache&&(d.animationCache={},d.animationCache.prevKey=null,d.animationCache.nextKey=null,d.animationCache.originalMatrix=c.matrix),c=this.data.hierarchy[a].keys,c.length&&(d.animationCache.prevKey=c[0],d.animationCache.nextKey="
                << std::endl ;
            out
                << "c[1],this.startTime=Math.min(c[0].time,this.startTime),this.endTime=Math.max(c[c.length-1].time,this.endTime));this.update(0)}this.isPaused=!1;THREE.AnimationHandler.play(this)},stop:function(){this.isPaused=this.isPlaying=!1;THREE.AnimationHandler.stop(this);for(var a=0;a<this.data.hierarchy.length;a++){var b=this.hierarchy[a],c=this.data.hierarchy[a];if(void 0!==c.animationCache){var d=c.animationCache.originalMatrix;d.copy(b.matrix);b.matrix=d;delete c.animationCache}}},update:function(a){if(!1!=="
                << std::endl ;
            out
                << "this.isPlaying){this.currentTime+=a*this.timeScale;a=this.data.length;!0===this.loop&&this.currentTime>a&&(this.currentTime%=a);this.currentTime=Math.min(this.currentTime,a);a=0;for(var b=this.hierarchy.length;a<b;a++){var c=this.hierarchy[a],d=this.data.hierarchy[a],e=d.keys,d=d.animationCache;if(e.length){var f=d.prevKey,g=d.nextKey;if(g.time<=this.currentTime){for(;g.time<this.currentTime&&g.index>f.index;)f=g,g=e[f.index+1];d.prevKey=f;d.nextKey=g}g.time>=this.currentTime?f.interpolate(g,this.currentTime):"
                << std::endl ;
            out
                << "f.interpolate(g,g.time);this.data.hierarchy[a].node.updateMatrix();c.matrixWorldNeedsUpdate=!0}}}},getNextKeyWith:function(a,b,c){b=this.data.hierarchy[b].keys;for(c%=b.length;c<b.length;c++)if(b[c].hasTarget(a))return b[c];return b[0]},getPrevKeyWith:function(a,b,c){b=this.data.hierarchy[b].keys;for(c=0<=c?c:c+b.length;0<=c;c--)if(b[c].hasTarget(a))return b[c];return b[b.length-1]}};"
                << std::endl ;
            out
                << "THREE.MorphAnimation=function(a){this.mesh=a;this.frames=a.morphTargetInfluences.length;this.currentTime=0;this.duration=1E3;this.loop=!0;this.currentFrame=this.lastFrame=0;this.isPlaying=!1};"
                << std::endl ;
            out
                << "THREE.MorphAnimation.prototype={constructor:THREE.MorphAnimation,play:function(){this.isPlaying=!0},pause:function(){this.isPlaying=!1},update:function(a){if(!1!==this.isPlaying){this.currentTime+=a;!0===this.loop&&this.currentTime>this.duration&&(this.currentTime%=this.duration);this.currentTime=Math.min(this.currentTime,this.duration);a=this.duration/this.frames;var b=Math.floor(this.currentTime/a),c=this.mesh.morphTargetInfluences;b!=this.currentFrame&&(c[this.lastFrame]=0,c[this.currentFrame]="
                << std::endl ;
            out
                << "1,c[b]=0,this.lastFrame=this.currentFrame,this.currentFrame=b);c[b]=this.currentTime%a/a;c[this.lastFrame]=1-c[b]}}};"
                << std::endl ;
            out
                << "THREE.BoxGeometry=function(a,b,c,d,e,f){function g(a,b,c,d,e,f,g,s){var u,v=h.widthSegments,x=h.heightSegments,D=e/2,w=f/2,y=h.vertices.length;if(\"x\"===a&&\"y\"===b||\"y\"===a&&\"x\"===b)u=\"z\";else if(\"x\"===a&&\"z\"===b||\"z\"===a&&\"x\"===b)u=\"y\",x=h.depthSegments;else if(\"z\"===a&&\"y\"===b||\"y\"===a&&\"z\"===b)u=\"x\",v=h.depthSegments;var A=v+1,E=x+1,G=e/v,F=f/x,z=new THREE.Vector3;z[u]=0<g?1:-1;for(e=0;e<E;e++)for(f=0;f<A;f++){var I=new THREE.Vector3;I[a]=(f*G-D)*c;I[b]=(e*F-w)*d;I[u]=g;h.vertices.push(I)}for(e="
                << std::endl ;
            out
                << "0;e<x;e++)for(f=0;f<v;f++)w=f+A*e,a=f+A*(e+1),b=f+1+A*(e+1),c=f+1+A*e,d=new THREE.Vector2(f/v,1-e/x),g=new THREE.Vector2(f/v,1-(e+1)/x),u=new THREE.Vector2((f+1)/v,1-(e+1)/x),D=new THREE.Vector2((f+1)/v,1-e/x),w=new THREE.Face3(w+y,a+y,c+y),w.normal.copy(z),w.vertexNormals.push(z.clone(),z.clone(),z.clone()),w.materialIndex=s,h.faces.push(w),h.faceVertexUvs[0].push([d,g,D]),w=new THREE.Face3(a+y,b+y,c+y),w.normal.copy(z),w.vertexNormals.push(z.clone(),z.clone(),z.clone()),w.materialIndex=s,h.faces.push(w),"
                << std::endl ;
            out
                << "h.faceVertexUvs[0].push([g.clone(),u,D.clone()])}THREE.Geometry.call(this);this.type=\"BoxGeometry\";this.parameters={width:a,height:b,depth:c,widthSegments:d,heightSegments:e,depthSegments:f};this.widthSegments=d||1;this.heightSegments=e||1;this.depthSegments=f||1;var h=this;d=a/2;e=b/2;f=c/2;g(\"z\",\"y\",-1,-1,c,b,d,0);g(\"z\",\"y\",1,-1,c,b,-d,1);g(\"x\",\"z\",1,1,a,c,e,2);g(\"x\",\"z\",1,-1,a,c,-e,3);g(\"x\",\"y\",1,-1,a,b,f,4);g(\"x\",\"y\",-1,-1,a,b,-f,5);this.mergeVertices()};THREE.BoxGeometry.prototype=Object.create(THREE.Geometry.prototype);"
                << std::endl ;
            out << "THREE.BoxGeometry.prototype.constructor=THREE.BoxGeometry;"
                << std::endl ;
            out
                << "THREE.CircleGeometry=function(a,b,c,d){THREE.Geometry.call(this);this.type=\"CircleGeometry\";this.parameters={radius:a,segments:b,thetaStart:c,thetaLength:d};a=a||50;b=void 0!==b?Math.max(3,b):8;c=void 0!==c?c:0;d=void 0!==d?d:2*Math.PI;var e,f=[];e=new THREE.Vector3;var g=new THREE.Vector2(.5,.5);this.vertices.push(e);f.push(g);for(e=0;e<=b;e++){var h=new THREE.Vector3,k=c+e/b*d;h.x=a*Math.cos(k);h.y=a*Math.sin(k);this.vertices.push(h);f.push(new THREE.Vector2((h.x/a+1)/2,(h.y/a+1)/2))}c=new THREE.Vector3(0,"
                << std::endl ;
            out
                << "0,1);for(e=1;e<=b;e++)this.faces.push(new THREE.Face3(e,e+1,0,[c.clone(),c.clone(),c.clone()])),this.faceVertexUvs[0].push([f[e].clone(),f[e+1].clone(),g.clone()]);this.computeFaceNormals();this.boundingSphere=new THREE.Sphere(new THREE.Vector3,a)};THREE.CircleGeometry.prototype=Object.create(THREE.Geometry.prototype);THREE.CircleGeometry.prototype.constructor=THREE.CircleGeometry;"
                << std::endl ;
            out
                << "THREE.CubeGeometry=function(a,b,c,d,e,f){THREE.warn(\"THREE.CubeGeometry has been renamed to THREE.BoxGeometry.\");return new THREE.BoxGeometry(a,b,c,d,e,f)};"
                << std::endl ;
            out
                << "THREE.CylinderGeometry=function(a,b,c,d,e,f,g,h){THREE.Geometry.call(this);this.type=\"CylinderGeometry\";this.parameters={radiusTop:a,radiusBottom:b,height:c,radialSegments:d,heightSegments:e,openEnded:f,thetaStart:g,thetaLength:h};a=void 0!==a?a:20;b=void 0!==b?b:20;c=void 0!==c?c:100;d=d||8;e=e||1;f=void 0!==f?f:!1;g=void 0!==g?g:0;h=void 0!==h?h:2*Math.PI;var k=c/2,l,p,q=[],n=[];for(p=0;p<=e;p++){var t=[],r=[],s=p/e,u=s*(b-a)+a;for(l=0;l<=d;l++){var v=l/d,x=new THREE.Vector3;x.x=u*Math.sin(v*h+"
                << std::endl ;
            out
                << "g);x.y=-s*c+k;x.z=u*Math.cos(v*h+g);this.vertices.push(x);t.push(this.vertices.length-1);r.push(new THREE.Vector2(v,1-s))}q.push(t);n.push(r)}c=(b-a)/c;for(l=0;l<d;l++)for(0!==a?(g=this.vertices[q[0][l]].clone(),h=this.vertices[q[0][l+1]].clone()):(g=this.vertices[q[1][l]].clone(),h=this.vertices[q[1][l+1]].clone()),g.setY(Math.sqrt(g.x*g.x+g.z*g.z)*c).normalize(),h.setY(Math.sqrt(h.x*h.x+h.z*h.z)*c).normalize(),p=0;p<e;p++){var t=q[p][l],r=q[p+1][l],s=q[p+1][l+1],u=q[p][l+1],v=g.clone(),x=g.clone(),"
                << std::endl ;
            out
                << "D=h.clone(),w=h.clone(),y=n[p][l].clone(),A=n[p+1][l].clone(),E=n[p+1][l+1].clone(),G=n[p][l+1].clone();this.faces.push(new THREE.Face3(t,r,u,[v,x,w]));this.faceVertexUvs[0].push([y,A,G]);this.faces.push(new THREE.Face3(r,s,u,[x.clone(),D,w.clone()]));this.faceVertexUvs[0].push([A.clone(),E,G.clone()])}if(!1===f&&0<a)for(this.vertices.push(new THREE.Vector3(0,k,0)),l=0;l<d;l++)t=q[0][l],r=q[0][l+1],s=this.vertices.length-1,v=new THREE.Vector3(0,1,0),x=new THREE.Vector3(0,1,0),D=new THREE.Vector3(0,"
                << std::endl ;
            out
                << "1,0),y=n[0][l].clone(),A=n[0][l+1].clone(),E=new THREE.Vector2(A.x,0),this.faces.push(new THREE.Face3(t,r,s,[v,x,D])),this.faceVertexUvs[0].push([y,A,E]);if(!1===f&&0<b)for(this.vertices.push(new THREE.Vector3(0,-k,0)),l=0;l<d;l++)t=q[e][l+1],r=q[e][l],s=this.vertices.length-1,v=new THREE.Vector3(0,-1,0),x=new THREE.Vector3(0,-1,0),D=new THREE.Vector3(0,-1,0),y=n[e][l+1].clone(),A=n[e][l].clone(),E=new THREE.Vector2(A.x,1),this.faces.push(new THREE.Face3(t,r,s,[v,x,D])),this.faceVertexUvs[0].push([y,"
                << std::endl ;
            out
                << "A,E]);this.computeFaceNormals()};THREE.CylinderGeometry.prototype=Object.create(THREE.Geometry.prototype);THREE.CylinderGeometry.prototype.constructor=THREE.CylinderGeometry;THREE.ExtrudeGeometry=function(a,b){\"undefined\"!==typeof a&&(THREE.Geometry.call(this),this.type=\"ExtrudeGeometry\",a=a instanceof Array?a:[a],this.addShapeList(a,b),this.computeFaceNormals())};THREE.ExtrudeGeometry.prototype=Object.create(THREE.Geometry.prototype);THREE.ExtrudeGeometry.prototype.constructor=THREE.ExtrudeGeometry;"
                << std::endl ;
            out
                << "THREE.ExtrudeGeometry.prototype.addShapeList=function(a,b){for(var c=a.length,d=0;d<c;d++)this.addShape(a[d],b)};"
                << std::endl ;
            out
                << "THREE.ExtrudeGeometry.prototype.addShape=function(a,b){function c(a,b,c){b||THREE.error(\"THREE.ExtrudeGeometry: vec does not exist\");return b.clone().multiplyScalar(c).add(a)}function d(a,b,c){var d=1,d=a.x-b.x,e=a.y-b.y,f=c.x-a.x,g=c.y-a.y,h=d*d+e*e;if(1E-10<Math.abs(d*g-e*f)){var k=Math.sqrt(h),l=Math.sqrt(f*f+g*g),h=b.x-e/k;b=b.y+d/k;f=((c.x-g/l-h)*g-(c.y+f/l-b)*f)/(d*g-e*f);c=h+d*f-a.x;a=b+e*f-a.y;d=c*c+a*a;if(2>=d)return new THREE.Vector2(c,a);d=Math.sqrt(d/2)}else a=!1,1E-10<d?1E-10<f&&(a=!0):"
                << std::endl ;
            out
                << "-1E-10>d?-1E-10>f&&(a=!0):Math.sign(e)==Math.sign(g)&&(a=!0),a?(c=-e,a=d,d=Math.sqrt(h)):(c=d,a=e,d=Math.sqrt(h/2));return new THREE.Vector2(c/d,a/d)}function e(a,b){var c,d;for(O=a.length;0<=--O;){c=O;d=O-1;0>d&&(d=a.length-1);for(var e=0,f=t+2*p,e=0;e<f;e++){var g=oa*e,h=oa*(e+1),k=b+c+g,g=b+d+g,l=b+d+h,h=b+c+h,k=k+U,g=g+U,l=l+U,h=h+U;I.faces.push(new THREE.Face3(k,g,h,null,null,x));I.faces.push(new THREE.Face3(g,l,h,null,null,x));k=D.generateSideWallUV(I,k,g,l,h);I.faceVertexUvs[0].push([k[0],"
                << std::endl ;
            out
                << "k[1],k[3]]);I.faceVertexUvs[0].push([k[1],k[2],k[3]])}}}function f(a,b,c){I.vertices.push(new THREE.Vector3(a,b,c))}function g(a,b,c){a+=U;b+=U;c+=U;I.faces.push(new THREE.Face3(a,b,c,null,null,v));a=D.generateTopUV(I,a,b,c);I.faceVertexUvs[0].push(a)}var h=void 0!==b.amount?b.amount:100,k=void 0!==b.bevelThickness?b.bevelThickness:6,l=void 0!==b.bevelSize?b.bevelSize:k-2,p=void 0!==b.bevelSegments?b.bevelSegments:3,q=void 0!==b.bevelEnabled?b.bevelEnabled:!0,n=void 0!==b.curveSegments?b.curveSegments:"
                << std::endl ;
            out
                << "12,t=void 0!==b.steps?b.steps:1,r=b.extrudePath,s,u=!1,v=b.material,x=b.extrudeMaterial,D=void 0!==b.UVGenerator?b.UVGenerator:THREE.ExtrudeGeometry.WorldUVGenerator,w,y,A,E;r&&(s=r.getSpacedPoints(t),u=!0,q=!1,w=void 0!==b.frames?b.frames:new THREE.TubeGeometry.FrenetFrames(r,t,!1),y=new THREE.Vector3,A=new THREE.Vector3,E=new THREE.Vector3);q||(l=k=p=0);var G,F,z,I=this,U=this.vertices.length,r=a.extractPoints(n),n=r.shape,M=r.holes;if(r=!THREE.Shape.Utils.isClockWise(n)){n=n.reverse();F=0;for(z="
                << std::endl ;
            out
                << "M.length;F<z;F++)G=M[F],THREE.Shape.Utils.isClockWise(G)&&(M[F]=G.reverse());r=!1}var H=THREE.Shape.Utils.triangulateShape(n,M),L=n;F=0;for(z=M.length;F<z;F++)G=M[F],n=n.concat(G);var P,N,R,V,J,oa=n.length,ja,ha=H.length,r=[],O=0;R=L.length;P=R-1;for(N=O+1;O<R;O++,P++,N++)P===R&&(P=0),N===R&&(N=0),r[O]=d(L[O],L[P],L[N]);var ca=[],ba,qa=r.concat();F=0;for(z=M.length;F<z;F++){G=M[F];ba=[];O=0;R=G.length;P=R-1;for(N=O+1;O<R;O++,P++,N++)P===R&&(P=0),N===R&&(N=0),ba[O]=d(G[O],G[P],G[N]);ca.push(ba);qa="
                << std::endl ;
            out
                << "qa.concat(ba)}for(P=0;P<p;P++){R=P/p;V=k*(1-R);N=l*Math.sin(R*Math.PI/2);O=0;for(R=L.length;O<R;O++)J=c(L[O],r[O],N),f(J.x,J.y,-V);F=0;for(z=M.length;F<z;F++)for(G=M[F],ba=ca[F],O=0,R=G.length;O<R;O++)J=c(G[O],ba[O],N),f(J.x,J.y,-V)}N=l;for(O=0;O<oa;O++)J=q?c(n[O],qa[O],N):n[O],u?(A.copy(w.normals[0]).multiplyScalar(J.x),y.copy(w.binormals[0]).multiplyScalar(J.y),E.copy(s[0]).add(A).add(y),f(E.x,E.y,E.z)):f(J.x,J.y,0);for(R=1;R<=t;R++)for(O=0;O<oa;O++)J=q?c(n[O],qa[O],N):n[O],u?(A.copy(w.normals[R]).multiplyScalar(J.x),"
                << std::endl ;
            out
                << "y.copy(w.binormals[R]).multiplyScalar(J.y),E.copy(s[R]).add(A).add(y),f(E.x,E.y,E.z)):f(J.x,J.y,h/t*R);for(P=p-1;0<=P;P--){R=P/p;V=k*(1-R);N=l*Math.sin(R*Math.PI/2);O=0;for(R=L.length;O<R;O++)J=c(L[O],r[O],N),f(J.x,J.y,h+V);F=0;for(z=M.length;F<z;F++)for(G=M[F],ba=ca[F],O=0,R=G.length;O<R;O++)J=c(G[O],ba[O],N),u?f(J.x,J.y+s[t-1].y,s[t-1].x+V):f(J.x,J.y,h+V)}(function(){if(q){var a;a=0*oa;for(O=0;O<ha;O++)ja=H[O],g(ja[2]+a,ja[1]+a,ja[0]+a);a=t+2*p;a*=oa;for(O=0;O<ha;O++)ja=H[O],g(ja[0]+a,ja[1]+a,ja[2]+"
                << std::endl ;
            out
                << "a)}else{for(O=0;O<ha;O++)ja=H[O],g(ja[2],ja[1],ja[0]);for(O=0;O<ha;O++)ja=H[O],g(ja[0]+oa*t,ja[1]+oa*t,ja[2]+oa*t)}})();(function(){var a=0;e(L,a);a+=L.length;F=0;for(z=M.length;F<z;F++)G=M[F],e(G,a),a+=G.length})()};"
                << std::endl ;
            out
                << "THREE.ExtrudeGeometry.WorldUVGenerator={generateTopUV:function(a,b,c,d){a=a.vertices;b=a[b];c=a[c];d=a[d];return[new THREE.Vector2(b.x,b.y),new THREE.Vector2(c.x,c.y),new THREE.Vector2(d.x,d.y)]},generateSideWallUV:function(a,b,c,d,e){a=a.vertices;b=a[b];c=a[c];d=a[d];e=a[e];return.01>Math.abs(b.y-c.y)?[new THREE.Vector2(b.x,1-b.z),new THREE.Vector2(c.x,1-c.z),new THREE.Vector2(d.x,1-d.z),new THREE.Vector2(e.x,1-e.z)]:[new THREE.Vector2(b.y,1-b.z),new THREE.Vector2(c.y,1-c.z),new THREE.Vector2(d.y,"
                << std::endl ;
            out
                << "1-d.z),new THREE.Vector2(e.y,1-e.z)]}};THREE.ShapeGeometry=function(a,b){THREE.Geometry.call(this);this.type=\"ShapeGeometry\";!1===a instanceof Array&&(a=[a]);this.addShapeList(a,b);this.computeFaceNormals()};THREE.ShapeGeometry.prototype=Object.create(THREE.Geometry.prototype);THREE.ShapeGeometry.prototype.constructor=THREE.ShapeGeometry;THREE.ShapeGeometry.prototype.addShapeList=function(a,b){for(var c=0,d=a.length;c<d;c++)this.addShape(a[c],b);return this};"
                << std::endl ;
            out
                << "THREE.ShapeGeometry.prototype.addShape=function(a,b){void 0===b&&(b={});var c=b.material,d=void 0===b.UVGenerator?THREE.ExtrudeGeometry.WorldUVGenerator:b.UVGenerator,e,f,g,h=this.vertices.length;e=a.extractPoints(void 0!==b.curveSegments?b.curveSegments:12);var k=e.shape,l=e.holes;if(!THREE.Shape.Utils.isClockWise(k))for(k=k.reverse(),e=0,f=l.length;e<f;e++)g=l[e],THREE.Shape.Utils.isClockWise(g)&&(l[e]=g.reverse());var p=THREE.Shape.Utils.triangulateShape(k,l);e=0;for(f=l.length;e<f;e++)g=l[e],"
                << std::endl ;
            out
                << "k=k.concat(g);l=k.length;f=p.length;for(e=0;e<l;e++)g=k[e],this.vertices.push(new THREE.Vector3(g.x,g.y,0));for(e=0;e<f;e++)l=p[e],k=l[0]+h,g=l[1]+h,l=l[2]+h,this.faces.push(new THREE.Face3(k,g,l,null,null,c)),this.faceVertexUvs[0].push(d.generateTopUV(this,k,g,l))};"
                << std::endl ;
            out
                << "THREE.LatheGeometry=function(a,b,c,d){THREE.Geometry.call(this);this.type=\"LatheGeometry\";this.parameters={points:a,segments:b,phiStart:c,phiLength:d};b=b||12;c=c||0;d=d||2*Math.PI;for(var e=1/(a.length-1),f=1/b,g=0,h=b;g<=h;g++)for(var k=c+g*f*d,l=Math.cos(k),p=Math.sin(k),k=0,q=a.length;k<q;k++){var n=a[k],t=new THREE.Vector3;t.x=l*n.x-p*n.y;t.y=p*n.x+l*n.y;t.z=n.z;this.vertices.push(t)}c=a.length;g=0;for(h=b;g<h;g++)for(k=0,q=a.length-1;k<q;k++){b=p=k+c*g;d=p+c;var l=p+1+c,p=p+1,n=g*f,t=k*e,r="
                << std::endl ;
            out
                << "n+f,s=t+e;this.faces.push(new THREE.Face3(b,d,p));this.faceVertexUvs[0].push([new THREE.Vector2(n,t),new THREE.Vector2(r,t),new THREE.Vector2(n,s)]);this.faces.push(new THREE.Face3(d,l,p));this.faceVertexUvs[0].push([new THREE.Vector2(r,t),new THREE.Vector2(r,s),new THREE.Vector2(n,s)])}this.mergeVertices();this.computeFaceNormals();this.computeVertexNormals()};THREE.LatheGeometry.prototype=Object.create(THREE.Geometry.prototype);THREE.LatheGeometry.prototype.constructor=THREE.LatheGeometry;"
                << std::endl ;
            out
                << "THREE.PlaneGeometry=function(a,b,c,d){console.info(\"THREE.PlaneGeometry: Consider using THREE.PlaneBufferGeometry for lower memory footprint.\");THREE.Geometry.call(this);this.type=\"PlaneGeometry\";this.parameters={width:a,height:b,widthSegments:c,heightSegments:d};this.fromBufferGeometry(new THREE.PlaneBufferGeometry(a,b,c,d))};THREE.PlaneGeometry.prototype=Object.create(THREE.Geometry.prototype);THREE.PlaneGeometry.prototype.constructor=THREE.PlaneGeometry;"
                << std::endl ;
            out
                << "THREE.PlaneBufferGeometry=function(a,b,c,d){THREE.BufferGeometry.call(this);this.type=\"PlaneBufferGeometry\";this.parameters={width:a,height:b,widthSegments:c,heightSegments:d};var e=a/2,f=b/2;c=c||1;d=d||1;var g=c+1,h=d+1,k=a/c,l=b/d;b=new Float32Array(g*h*3);a=new Float32Array(g*h*3);for(var p=new Float32Array(g*h*2),q=0,n=0,t=0;t<h;t++)for(var r=t*l-f,s=0;s<g;s++)b[q]=s*k-e,b[q+1]=-r,a[q+2]=1,p[n]=s/c,p[n+1]=1-t/d,q+=3,n+=2;q=0;e=new (65535<b.length/3?Uint32Array:Uint16Array)(c*d*6);for(t=0;t<d;t++)for(s="
                << std::endl ;
            out
                << "0;s<c;s++)f=s+g*(t+1),h=s+1+g*(t+1),k=s+1+g*t,e[q]=s+g*t,e[q+1]=f,e[q+2]=k,e[q+3]=f,e[q+4]=h,e[q+5]=k,q+=6;this.addAttribute(\"index\",new THREE.BufferAttribute(e,1));this.addAttribute(\"position\",new THREE.BufferAttribute(b,3));this.addAttribute(\"normal\",new THREE.BufferAttribute(a,3));this.addAttribute(\"uv\",new THREE.BufferAttribute(p,2))};THREE.PlaneBufferGeometry.prototype=Object.create(THREE.BufferGeometry.prototype);THREE.PlaneBufferGeometry.prototype.constructor=THREE.PlaneBufferGeometry;"
                << std::endl ;
            out
                << "THREE.RingGeometry=function(a,b,c,d,e,f){THREE.Geometry.call(this);this.type=\"RingGeometry\";this.parameters={innerRadius:a,outerRadius:b,thetaSegments:c,phiSegments:d,thetaStart:e,thetaLength:f};a=a||0;b=b||50;e=void 0!==e?e:0;f=void 0!==f?f:2*Math.PI;c=void 0!==c?Math.max(3,c):8;d=void 0!==d?Math.max(1,d):8;var g,h=[],k=a,l=(b-a)/d;for(a=0;a<d+1;a++){for(g=0;g<c+1;g++){var p=new THREE.Vector3,q=e+g/c*f;p.x=k*Math.cos(q);p.y=k*Math.sin(q);this.vertices.push(p);h.push(new THREE.Vector2((p.x/b+1)/2,"
                << std::endl ;
            out
                << "(p.y/b+1)/2))}k+=l}b=new THREE.Vector3(0,0,1);for(a=0;a<d;a++)for(e=a*(c+1),g=0;g<c;g++)f=q=g+e,l=q+c+1,p=q+c+2,this.faces.push(new THREE.Face3(f,l,p,[b.clone(),b.clone(),b.clone()])),this.faceVertexUvs[0].push([h[f].clone(),h[l].clone(),h[p].clone()]),f=q,l=q+c+2,p=q+1,this.faces.push(new THREE.Face3(f,l,p,[b.clone(),b.clone(),b.clone()])),this.faceVertexUvs[0].push([h[f].clone(),h[l].clone(),h[p].clone()]);this.computeFaceNormals();this.boundingSphere=new THREE.Sphere(new THREE.Vector3,k)};"
                << std::endl ;
            out
                << "THREE.RingGeometry.prototype=Object.create(THREE.Geometry.prototype);THREE.RingGeometry.prototype.constructor=THREE.RingGeometry;"
                << std::endl ;
            out
                << "THREE.SphereGeometry=function(a,b,c,d,e,f,g){THREE.Geometry.call(this);this.type=\"SphereGeometry\";this.parameters={radius:a,widthSegments:b,heightSegments:c,phiStart:d,phiLength:e,thetaStart:f,thetaLength:g};a=a||50;b=Math.max(3,Math.floor(b)||8);c=Math.max(2,Math.floor(c)||6);d=void 0!==d?d:0;e=void 0!==e?e:2*Math.PI;f=void 0!==f?f:0;g=void 0!==g?g:Math.PI;var h,k,l=[],p=[];for(k=0;k<=c;k++){var q=[],n=[];for(h=0;h<=b;h++){var t=h/b,r=k/c,s=new THREE.Vector3;s.x=-a*Math.cos(d+t*e)*Math.sin(f+r*g);"
                << std::endl ;
            out
                << "s.y=a*Math.cos(f+r*g);s.z=a*Math.sin(d+t*e)*Math.sin(f+r*g);this.vertices.push(s);q.push(this.vertices.length-1);n.push(new THREE.Vector2(t,1-r))}l.push(q);p.push(n)}for(k=0;k<c;k++)for(h=0;h<b;h++){d=l[k][h+1];e=l[k][h];f=l[k+1][h];g=l[k+1][h+1];var q=this.vertices[d].clone().normalize(),n=this.vertices[e].clone().normalize(),t=this.vertices[f].clone().normalize(),r=this.vertices[g].clone().normalize(),s=p[k][h+1].clone(),u=p[k][h].clone(),v=p[k+1][h].clone(),x=p[k+1][h+1].clone();Math.abs(this.vertices[d].y)==="
                << std::endl ;
            out
                << "a?(s.x=(s.x+u.x)/2,this.faces.push(new THREE.Face3(d,f,g,[q,t,r])),this.faceVertexUvs[0].push([s,v,x])):Math.abs(this.vertices[f].y)===a?(v.x=(v.x+x.x)/2,this.faces.push(new THREE.Face3(d,e,f,[q,n,t])),this.faceVertexUvs[0].push([s,u,v])):(this.faces.push(new THREE.Face3(d,e,g,[q,n,r])),this.faceVertexUvs[0].push([s,u,x]),this.faces.push(new THREE.Face3(e,f,g,[n.clone(),t,r.clone()])),this.faceVertexUvs[0].push([u.clone(),v,x.clone()]))}this.computeFaceNormals();this.boundingSphere=new THREE.Sphere(new THREE.Vector3,"
                << std::endl ;
            out
                << "a)};THREE.SphereGeometry.prototype=Object.create(THREE.Geometry.prototype);THREE.SphereGeometry.prototype.constructor=THREE.SphereGeometry;THREE.TextGeometry=function(a,b){b=b||{};var c=THREE.FontUtils.generateShapes(a,b);b.amount=void 0!==b.height?b.height:50;void 0===b.bevelThickness&&(b.bevelThickness=10);void 0===b.bevelSize&&(b.bevelSize=8);void 0===b.bevelEnabled&&(b.bevelEnabled=!1);THREE.ExtrudeGeometry.call(this,c,b);this.type=\"TextGeometry\"};THREE.TextGeometry.prototype=Object.create(THREE.ExtrudeGeometry.prototype);"
                << std::endl ;
            out << "THREE.TextGeometry.prototype.constructor=THREE.TextGeometry;"
                << std::endl ;
            out
                << "THREE.TorusGeometry=function(a,b,c,d,e){THREE.Geometry.call(this);this.type=\"TorusGeometry\";this.parameters={radius:a,tube:b,radialSegments:c,tubularSegments:d,arc:e};a=a||100;b=b||40;c=c||8;d=d||6;e=e||2*Math.PI;for(var f=new THREE.Vector3,g=[],h=[],k=0;k<=c;k++)for(var l=0;l<=d;l++){var p=l/d*e,q=k/c*Math.PI*2;f.x=a*Math.cos(p);f.y=a*Math.sin(p);var n=new THREE.Vector3;n.x=(a+b*Math.cos(q))*Math.cos(p);n.y=(a+b*Math.cos(q))*Math.sin(p);n.z=b*Math.sin(q);this.vertices.push(n);g.push(new THREE.Vector2(l/"
                << std::endl ;
            out
                << "d,k/c));h.push(n.clone().sub(f).normalize())}for(k=1;k<=c;k++)for(l=1;l<=d;l++)a=(d+1)*k+l-1,b=(d+1)*(k-1)+l-1,e=(d+1)*(k-1)+l,f=(d+1)*k+l,p=new THREE.Face3(a,b,f,[h[a].clone(),h[b].clone(),h[f].clone()]),this.faces.push(p),this.faceVertexUvs[0].push([g[a].clone(),g[b].clone(),g[f].clone()]),p=new THREE.Face3(b,e,f,[h[b].clone(),h[e].clone(),h[f].clone()]),this.faces.push(p),this.faceVertexUvs[0].push([g[b].clone(),g[e].clone(),g[f].clone()]);this.computeFaceNormals()};"
                << std::endl ;
            out
                << "THREE.TorusGeometry.prototype=Object.create(THREE.Geometry.prototype);THREE.TorusGeometry.prototype.constructor=THREE.TorusGeometry;"
                << std::endl ;
            out
                << "THREE.TorusKnotGeometry=function(a,b,c,d,e,f,g){function h(a,b,c,d,e){var f=Math.cos(a),g=Math.sin(a);a*=b/c;b=Math.cos(a);f*=d*(2+b)*.5;g=d*(2+b)*g*.5;d=e*d*Math.sin(a)*.5;return new THREE.Vector3(f,g,d)}THREE.Geometry.call(this);this.type=\"TorusKnotGeometry\";this.parameters={radius:a,tube:b,radialSegments:c,tubularSegments:d,p:e,q:f,heightScale:g};a=a||100;b=b||40;c=c||64;d=d||8;e=e||2;f=f||3;g=g||1;for(var k=Array(c),l=new THREE.Vector3,p=new THREE.Vector3,q=new THREE.Vector3,n=0;n<c;++n){k[n]="
                << std::endl ;
            out
                << "Array(d);var t=n/c*2*e*Math.PI,r=h(t,f,e,a,g),t=h(t+.01,f,e,a,g);l.subVectors(t,r);p.addVectors(t,r);q.crossVectors(l,p);p.crossVectors(q,l);q.normalize();p.normalize();for(t=0;t<d;++t){var s=t/d*2*Math.PI,u=-b*Math.cos(s),s=b*Math.sin(s),v=new THREE.Vector3;v.x=r.x+u*p.x+s*q.x;v.y=r.y+u*p.y+s*q.y;v.z=r.z+u*p.z+s*q.z;k[n][t]=this.vertices.push(v)-1}}for(n=0;n<c;++n)for(t=0;t<d;++t)e=(n+1)%c,f=(t+1)%d,a=k[n][t],b=k[e][t],e=k[e][f],f=k[n][f],g=new THREE.Vector2(n/c,t/d),l=new THREE.Vector2((n+1)/c,"
                << std::endl ;
            out
                << "t/d),p=new THREE.Vector2((n+1)/c,(t+1)/d),q=new THREE.Vector2(n/c,(t+1)/d),this.faces.push(new THREE.Face3(a,b,f)),this.faceVertexUvs[0].push([g,l,q]),this.faces.push(new THREE.Face3(b,e,f)),this.faceVertexUvs[0].push([l.clone(),p,q.clone()]);this.computeFaceNormals();this.computeVertexNormals()};THREE.TorusKnotGeometry.prototype=Object.create(THREE.Geometry.prototype);THREE.TorusKnotGeometry.prototype.constructor=THREE.TorusKnotGeometry;"
                << std::endl ;
            out
                << "THREE.TubeGeometry=function(a,b,c,d,e,f){THREE.Geometry.call(this);this.type=\"TubeGeometry\";this.parameters={path:a,segments:b,radius:c,radialSegments:d,closed:e};b=b||64;c=c||1;d=d||8;e=e||!1;f=f||THREE.TubeGeometry.NoTaper;var g=[],h,k,l=b+1,p,q,n,t,r,s=new THREE.Vector3,u,v,x;u=new THREE.TubeGeometry.FrenetFrames(a,b,e);v=u.normals;x=u.binormals;this.tangents=u.tangents;this.normals=v;this.binormals=x;for(u=0;u<l;u++)for(g[u]=[],p=u/(l-1),r=a.getPointAt(p),h=v[u],k=x[u],n=c*f(p),p=0;p<d;p++)q="
                << std::endl ;
            out
                << "p/d*2*Math.PI,t=-n*Math.cos(q),q=n*Math.sin(q),s.copy(r),s.x+=t*h.x+q*k.x,s.y+=t*h.y+q*k.y,s.z+=t*h.z+q*k.z,g[u][p]=this.vertices.push(new THREE.Vector3(s.x,s.y,s.z))-1;for(u=0;u<b;u++)for(p=0;p<d;p++)f=e?(u+1)%b:u+1,l=(p+1)%d,a=g[u][p],c=g[f][p],f=g[f][l],l=g[u][l],s=new THREE.Vector2(u/b,p/d),v=new THREE.Vector2((u+1)/b,p/d),x=new THREE.Vector2((u+1)/b,(p+1)/d),h=new THREE.Vector2(u/b,(p+1)/d),this.faces.push(new THREE.Face3(a,c,l)),this.faceVertexUvs[0].push([s,v,h]),this.faces.push(new THREE.Face3(c,"
                << std::endl ;
            out
                << "f,l)),this.faceVertexUvs[0].push([v.clone(),x,h.clone()]);this.computeFaceNormals();this.computeVertexNormals()};THREE.TubeGeometry.prototype=Object.create(THREE.Geometry.prototype);THREE.TubeGeometry.prototype.constructor=THREE.TubeGeometry;THREE.TubeGeometry.NoTaper=function(a){return 1};THREE.TubeGeometry.SinusoidalTaper=function(a){return Math.sin(Math.PI*a)};"
                << std::endl ;
            out
                << "THREE.TubeGeometry.FrenetFrames=function(a,b,c){var d=new THREE.Vector3,e=[],f=[],g=[],h=new THREE.Vector3,k=new THREE.Matrix4;b+=1;var l,p,q;this.tangents=e;this.normals=f;this.binormals=g;for(l=0;l<b;l++)p=l/(b-1),e[l]=a.getTangentAt(p),e[l].normalize();f[0]=new THREE.Vector3;g[0]=new THREE.Vector3;a=Number.MAX_VALUE;l=Math.abs(e[0].x);p=Math.abs(e[0].y);q=Math.abs(e[0].z);l<=a&&(a=l,d.set(1,0,0));p<=a&&(a=p,d.set(0,1,0));q<=a&&d.set(0,0,1);h.crossVectors(e[0],d).normalize();f[0].crossVectors(e[0],"
                << std::endl ;
            out
                << "h);g[0].crossVectors(e[0],f[0]);for(l=1;l<b;l++)f[l]=f[l-1].clone(),g[l]=g[l-1].clone(),h.crossVectors(e[l-1],e[l]),1E-4<h.length()&&(h.normalize(),d=Math.acos(THREE.Math.clamp(e[l-1].dot(e[l]),-1,1)),f[l].applyMatrix4(k.makeRotationAxis(h,d))),g[l].crossVectors(e[l],f[l]);if(c)for(d=Math.acos(THREE.Math.clamp(f[0].dot(f[b-1]),-1,1)),d/=b-1,0<e[0].dot(h.crossVectors(f[0],f[b-1]))&&(d=-d),l=1;l<b;l++)f[l].applyMatrix4(k.makeRotationAxis(e[l],d*l)),g[l].crossVectors(e[l],f[l])};"
                << std::endl ;
            out
                << "THREE.PolyhedronGeometry=function(a,b,c,d){function e(a){var b=a.normalize().clone();b.index=k.vertices.push(b)-1;var c=Math.atan2(a.z,-a.x)/2/Math.PI+.5;a=Math.atan2(-a.y,Math.sqrt(a.x*a.x+a.z*a.z))/Math.PI+.5;b.uv=new THREE.Vector2(c,1-a);return b}function f(a,b,c){var d=new THREE.Face3(a.index,b.index,c.index,[a.clone(),b.clone(),c.clone()]);k.faces.push(d);u.copy(a).add(b).add(c).divideScalar(3);d=Math.atan2(u.z,-u.x);k.faceVertexUvs[0].push([h(a.uv,a,d),h(b.uv,b,d),h(c.uv,c,d)])}function g(a,"
                << std::endl ;
            out
                << "b){for(var c=Math.pow(2,b),d=e(k.vertices[a.a]),g=e(k.vertices[a.b]),h=e(k.vertices[a.c]),l=[],n=0;n<=c;n++){l[n]=[];for(var p=e(d.clone().lerp(h,n/c)),q=e(g.clone().lerp(h,n/c)),s=c-n,r=0;r<=s;r++)l[n][r]=0==r&&n==c?p:e(p.clone().lerp(q,r/s))}for(n=0;n<c;n++)for(r=0;r<2*(c-n)-1;r++)d=Math.floor(r/2),0==r%2?f(l[n][d+1],l[n+1][d],l[n][d]):f(l[n][d+1],l[n+1][d+1],l[n+1][d])}function h(a,b,c){0>c&&1===a.x&&(a=new THREE.Vector2(a.x-1,a.y));0===b.x&&0===b.z&&(a=new THREE.Vector2(c/2/Math.PI+.5,a.y));return a.clone()}"
                << std::endl ;
            out
                << "THREE.Geometry.call(this);this.type=\"PolyhedronGeometry\";this.parameters={vertices:a,indices:b,radius:c,detail:d};c=c||1;d=d||0;for(var k=this,l=0,p=a.length;l<p;l+=3)e(new THREE.Vector3(a[l],a[l+1],a[l+2]));a=this.vertices;for(var q=[],n=l=0,p=b.length;l<p;l+=3,n++){var t=a[b[l]],r=a[b[l+1]],s=a[b[l+2]];q[n]=new THREE.Face3(t.index,r.index,s.index,[t.clone(),r.clone(),s.clone()])}for(var u=new THREE.Vector3,l=0,p=q.length;l<p;l++)g(q[l],d);l=0;for(p=this.faceVertexUvs[0].length;l<p;l++)b=this.faceVertexUvs[0][l],"
                << std::endl ;
            out
                << "d=b[0].x,a=b[1].x,q=b[2].x,n=Math.max(d,Math.max(a,q)),t=Math.min(d,Math.min(a,q)),.9<n&&.1>t&&(.2>d&&(b[0].x+=1),.2>a&&(b[1].x+=1),.2>q&&(b[2].x+=1));l=0;for(p=this.vertices.length;l<p;l++)this.vertices[l].multiplyScalar(c);this.mergeVertices();this.computeFaceNormals();this.boundingSphere=new THREE.Sphere(new THREE.Vector3,c)};THREE.PolyhedronGeometry.prototype=Object.create(THREE.Geometry.prototype);THREE.PolyhedronGeometry.prototype.constructor=THREE.PolyhedronGeometry;"
                << std::endl ;
            out
                << "THREE.DodecahedronGeometry=function(a,b){this.parameters={radius:a,detail:b};var c=(1+Math.sqrt(5))/2,d=1/c;THREE.PolyhedronGeometry.call(this,[-1,-1,-1,-1,-1,1,-1,1,-1,-1,1,1,1,-1,-1,1,-1,1,1,1,-1,1,1,1,0,-d,-c,0,-d,c,0,d,-c,0,d,c,-d,-c,0,-d,c,0,d,-c,0,d,c,0,-c,0,-d,c,0,-d,-c,0,d,c,0,d],[3,11,7,3,7,15,3,15,13,7,19,17,7,17,6,7,6,15,17,4,8,17,8,10,17,10,6,8,0,16,8,16,2,8,2,10,0,12,1,0,1,18,0,18,16,6,10,2,6,2,13,6,13,15,2,16,18,2,18,3,2,3,13,18,1,9,18,9,11,18,11,3,4,14,12,4,12,0,4,0,8,11,9,5,11,5,19,"
                << std::endl ;
            out
                << "11,19,7,19,5,14,19,14,4,19,4,17,1,12,14,1,14,5,1,5,9],a,b)};THREE.DodecahedronGeometry.prototype=Object.create(THREE.Geometry.prototype);THREE.DodecahedronGeometry.prototype.constructor=THREE.DodecahedronGeometry;"
                << std::endl ;
            out
                << "THREE.IcosahedronGeometry=function(a,b){var c=(1+Math.sqrt(5))/2;THREE.PolyhedronGeometry.call(this,[-1,c,0,1,c,0,-1,-c,0,1,-c,0,0,-1,c,0,1,c,0,-1,-c,0,1,-c,c,0,-1,c,0,1,-c,0,-1,-c,0,1],[0,11,5,0,5,1,0,1,7,0,7,10,0,10,11,1,5,9,5,11,4,11,10,2,10,7,6,7,1,8,3,9,4,3,4,2,3,2,6,3,6,8,3,8,9,4,9,5,2,4,11,6,2,10,8,6,7,9,8,1],a,b);this.type=\"IcosahedronGeometry\";this.parameters={radius:a,detail:b}};THREE.IcosahedronGeometry.prototype=Object.create(THREE.Geometry.prototype);"
                << std::endl ;
            out
                << "THREE.IcosahedronGeometry.prototype.constructor=THREE.IcosahedronGeometry;THREE.OctahedronGeometry=function(a,b){this.parameters={radius:a,detail:b};THREE.PolyhedronGeometry.call(this,[1,0,0,-1,0,0,0,1,0,0,-1,0,0,0,1,0,0,-1],[0,2,4,0,4,3,0,3,5,0,5,2,1,2,5,1,5,3,1,3,4,1,4,2],a,b);this.type=\"OctahedronGeometry\";this.parameters={radius:a,detail:b}};THREE.OctahedronGeometry.prototype=Object.create(THREE.Geometry.prototype);THREE.OctahedronGeometry.prototype.constructor=THREE.OctahedronGeometry;"
                << std::endl ;
            out
                << "THREE.TetrahedronGeometry=function(a,b){THREE.PolyhedronGeometry.call(this,[1,1,1,-1,-1,1,-1,1,-1,1,-1,-1],[2,1,0,0,3,2,1,3,0,2,3,1],a,b);this.type=\"TetrahedronGeometry\";this.parameters={radius:a,detail:b}};THREE.TetrahedronGeometry.prototype=Object.create(THREE.Geometry.prototype);THREE.TetrahedronGeometry.prototype.constructor=THREE.TetrahedronGeometry;"
                << std::endl ;
            out
                << "THREE.ParametricGeometry=function(a,b,c){THREE.Geometry.call(this);this.type=\"ParametricGeometry\";this.parameters={func:a,slices:b,stacks:c};var d=this.vertices,e=this.faces,f=this.faceVertexUvs[0],g,h,k,l,p=b+1;for(g=0;g<=c;g++)for(l=g/c,h=0;h<=b;h++)k=h/b,k=a(k,l),d.push(k);var q,n,t,r;for(g=0;g<c;g++)for(h=0;h<b;h++)a=g*p+h,d=g*p+h+1,l=(g+1)*p+h+1,k=(g+1)*p+h,q=new THREE.Vector2(h/b,g/c),n=new THREE.Vector2((h+1)/b,g/c),t=new THREE.Vector2((h+1)/b,(g+1)/c),r=new THREE.Vector2(h/b,(g+1)/c),e.push(new THREE.Face3(a,"
                << std::endl ;
            out
                << "d,k)),f.push([q,n,r]),e.push(new THREE.Face3(d,l,k)),f.push([n.clone(),t,r.clone()]);this.computeFaceNormals();this.computeVertexNormals()};THREE.ParametricGeometry.prototype=Object.create(THREE.Geometry.prototype);THREE.ParametricGeometry.prototype.constructor=THREE.ParametricGeometry;"
                << std::endl ;
            out
                << "THREE.AxisHelper=function(a){a=a||1;var b=new Float32Array([0,0,0,a,0,0,0,0,0,0,a,0,0,0,0,0,0,a]),c=new Float32Array([1,0,0,1,.6,0,0,1,0,.6,1,0,0,0,1,0,.6,1]);a=new THREE.BufferGeometry;a.addAttribute(\"position\",new THREE.BufferAttribute(b,3));a.addAttribute(\"color\",new THREE.BufferAttribute(c,3));b=new THREE.LineBasicMaterial({vertexColors:THREE.VertexColors});THREE.Line.call(this,a,b,THREE.LinePieces)};THREE.AxisHelper.prototype=Object.create(THREE.Line.prototype);"
                << std::endl ;
            out << "THREE.AxisHelper.prototype.constructor=THREE.AxisHelper;"
                << std::endl ;
            out
                << "THREE.ArrowHelper=function(){var a=new THREE.Geometry;a.vertices.push(new THREE.Vector3(0,0,0),new THREE.Vector3(0,1,0));var b=new THREE.CylinderGeometry(0,.5,1,5,1);b.applyMatrix((new THREE.Matrix4).makeTranslation(0,-.5,0));return function(c,d,e,f,g,h){THREE.Object3D.call(this);void 0===f&&(f=16776960);void 0===e&&(e=1);void 0===g&&(g=.2*e);void 0===h&&(h=.2*g);this.position.copy(d);this.line=new THREE.Line(a,new THREE.LineBasicMaterial({color:f}));this.line.matrixAutoUpdate=!1;this.add(this.line);"
                << std::endl ;
            out
                << "this.cone=new THREE.Mesh(b,new THREE.MeshBasicMaterial({color:f}));this.cone.matrixAutoUpdate=!1;this.add(this.cone);this.setDirection(c);this.setLength(e,g,h)}}();THREE.ArrowHelper.prototype=Object.create(THREE.Object3D.prototype);THREE.ArrowHelper.prototype.constructor=THREE.ArrowHelper;"
                << std::endl ;
            out
                << "THREE.ArrowHelper.prototype.setDirection=function(){var a=new THREE.Vector3,b;return function(c){.99999<c.y?this.quaternion.set(0,0,0,1):-.99999>c.y?this.quaternion.set(1,0,0,0):(a.set(c.z,0,-c.x).normalize(),b=Math.acos(c.y),this.quaternion.setFromAxisAngle(a,b))}}();THREE.ArrowHelper.prototype.setLength=function(a,b,c){void 0===b&&(b=.2*a);void 0===c&&(c=.2*b);this.line.scale.set(1,a-b,1);this.line.updateMatrix();this.cone.scale.set(c,b,c);this.cone.position.y=a;this.cone.updateMatrix()};"
                << std::endl ;
            out
                << "THREE.ArrowHelper.prototype.setColor=function(a){this.line.material.color.set(a);this.cone.material.color.set(a)};THREE.BoxHelper=function(a){var b=new THREE.BufferGeometry;b.addAttribute(\"position\",new THREE.BufferAttribute(new Float32Array(72),3));THREE.Line.call(this,b,new THREE.LineBasicMaterial({color:16776960}),THREE.LinePieces);void 0!==a&&this.update(a)};THREE.BoxHelper.prototype=Object.create(THREE.Line.prototype);THREE.BoxHelper.prototype.constructor=THREE.BoxHelper;"
                << std::endl ;
            out
                << "THREE.BoxHelper.prototype.update=function(a){var b=a.geometry;null===b.boundingBox&&b.computeBoundingBox();var c=b.boundingBox.min,b=b.boundingBox.max,d=this.geometry.attributes.position.array;d[0]=b.x;d[1]=b.y;d[2]=b.z;d[3]=c.x;d[4]=b.y;d[5]=b.z;d[6]=c.x;d[7]=b.y;d[8]=b.z;d[9]=c.x;d[10]=c.y;d[11]=b.z;d[12]=c.x;d[13]=c.y;d[14]=b.z;d[15]=b.x;d[16]=c.y;d[17]=b.z;d[18]=b.x;d[19]=c.y;d[20]=b.z;d[21]=b.x;d[22]=b.y;d[23]=b.z;d[24]=b.x;d[25]=b.y;d[26]=c.z;d[27]=c.x;d[28]=b.y;d[29]=c.z;d[30]=c.x;d[31]=b.y;"
                << std::endl ;
            out
                << "d[32]=c.z;d[33]=c.x;d[34]=c.y;d[35]=c.z;d[36]=c.x;d[37]=c.y;d[38]=c.z;d[39]=b.x;d[40]=c.y;d[41]=c.z;d[42]=b.x;d[43]=c.y;d[44]=c.z;d[45]=b.x;d[46]=b.y;d[47]=c.z;d[48]=b.x;d[49]=b.y;d[50]=b.z;d[51]=b.x;d[52]=b.y;d[53]=c.z;d[54]=c.x;d[55]=b.y;d[56]=b.z;d[57]=c.x;d[58]=b.y;d[59]=c.z;d[60]=c.x;d[61]=c.y;d[62]=b.z;d[63]=c.x;d[64]=c.y;d[65]=c.z;d[66]=b.x;d[67]=c.y;d[68]=b.z;d[69]=b.x;d[70]=c.y;d[71]=c.z;this.geometry.attributes.position.needsUpdate=!0;this.geometry.computeBoundingSphere();this.matrix=a.matrixWorld;"
                << std::endl ;
            out
                << "this.matrixAutoUpdate=!1};THREE.BoundingBoxHelper=function(a,b){var c=void 0!==b?b:8947848;this.object=a;this.box=new THREE.Box3;THREE.Mesh.call(this,new THREE.BoxGeometry(1,1,1),new THREE.MeshBasicMaterial({color:c,wireframe:!0}))};THREE.BoundingBoxHelper.prototype=Object.create(THREE.Mesh.prototype);THREE.BoundingBoxHelper.prototype.constructor=THREE.BoundingBoxHelper;THREE.BoundingBoxHelper.prototype.update=function(){this.box.setFromObject(this.object);this.box.size(this.scale);this.box.center(this.position)};"
                << std::endl ;
            out
                << "THREE.CameraHelper=function(a){function b(a,b,d){c(a,d);c(b,d)}function c(a,b){d.vertices.push(new THREE.Vector3);d.colors.push(new THREE.Color(b));void 0===f[a]&&(f[a]=[]);f[a].push(d.vertices.length-1)}var d=new THREE.Geometry,e=new THREE.LineBasicMaterial({color:16777215,vertexColors:THREE.FaceColors}),f={};b(\"n1\",\"n2\",16755200);b(\"n2\",\"n4\",16755200);b(\"n4\",\"n3\",16755200);b(\"n3\",\"n1\",16755200);b(\"f1\",\"f2\",16755200);b(\"f2\",\"f4\",16755200);b(\"f4\",\"f3\",16755200);b(\"f3\",\"f1\",16755200);b(\"n1\",\"f1\",16755200);"
                << std::endl ;
            out
                << "b(\"n2\",\"f2\",16755200);b(\"n3\",\"f3\",16755200);b(\"n4\",\"f4\",16755200);b(\"p\",\"n1\",16711680);b(\"p\",\"n2\",16711680);b(\"p\",\"n3\",16711680);b(\"p\",\"n4\",16711680);b(\"u1\",\"u2\",43775);b(\"u2\",\"u3\",43775);b(\"u3\",\"u1\",43775);b(\"c\",\"t\",16777215);b(\"p\",\"c\",3355443);b(\"cn1\",\"cn2\",3355443);b(\"cn3\",\"cn4\",3355443);b(\"cf1\",\"cf2\",3355443);b(\"cf3\",\"cf4\",3355443);THREE.Line.call(this,d,e,THREE.LinePieces);this.camera=a;this.matrix=a.matrixWorld;this.matrixAutoUpdate=!1;this.pointMap=f;this.update()};"
                << std::endl ;
            out
                << "THREE.CameraHelper.prototype=Object.create(THREE.Line.prototype);THREE.CameraHelper.prototype.constructor=THREE.CameraHelper;"
                << std::endl ;
            out
                << "THREE.CameraHelper.prototype.update=function(){var a,b,c=new THREE.Vector3,d=new THREE.Camera,e=function(e,g,h,k){c.set(g,h,k).unproject(d);e=b[e];if(void 0!==e)for(g=0,h=e.length;g<h;g++)a.vertices[e[g]].copy(c)};return function(){a=this.geometry;b=this.pointMap;d.projectionMatrix.copy(this.camera.projectionMatrix);e(\"c\",0,0,-1);e(\"t\",0,0,1);e(\"n1\",-1,-1,-1);e(\"n2\",1,-1,-1);e(\"n3\",-1,1,-1);e(\"n4\",1,1,-1);e(\"f1\",-1,-1,1);e(\"f2\",1,-1,1);e(\"f3\",-1,1,1);e(\"f4\",1,1,1);e(\"u1\",.7,1.1,-1);e(\"u2\",-.7,1.1,"
                << std::endl ;
            out
                << "-1);e(\"u3\",0,2,-1);e(\"cf1\",-1,0,1);e(\"cf2\",1,0,1);e(\"cf3\",0,-1,1);e(\"cf4\",0,1,1);e(\"cn1\",-1,0,-1);e(\"cn2\",1,0,-1);e(\"cn3\",0,-1,-1);e(\"cn4\",0,1,-1);a.verticesNeedUpdate=!0}}();"
                << std::endl ;
            out
                << "THREE.DirectionalLightHelper=function(a,b){THREE.Object3D.call(this);this.light=a;this.light.updateMatrixWorld();this.matrix=a.matrixWorld;this.matrixAutoUpdate=!1;b=b||1;var c=new THREE.Geometry;c.vertices.push(new THREE.Vector3(-b,b,0),new THREE.Vector3(b,b,0),new THREE.Vector3(b,-b,0),new THREE.Vector3(-b,-b,0),new THREE.Vector3(-b,b,0));var d=new THREE.LineBasicMaterial({fog:!1});d.color.copy(this.light.color).multiplyScalar(this.light.intensity);this.lightPlane=new THREE.Line(c,d);this.add(this.lightPlane);"
                << std::endl ;
            out
                << "c=new THREE.Geometry;c.vertices.push(new THREE.Vector3,new THREE.Vector3);d=new THREE.LineBasicMaterial({fog:!1});d.color.copy(this.light.color).multiplyScalar(this.light.intensity);this.targetLine=new THREE.Line(c,d);this.add(this.targetLine);this.update()};THREE.DirectionalLightHelper.prototype=Object.create(THREE.Object3D.prototype);THREE.DirectionalLightHelper.prototype.constructor=THREE.DirectionalLightHelper;"
                << std::endl ;
            out
                << "THREE.DirectionalLightHelper.prototype.dispose=function(){this.lightPlane.geometry.dispose();this.lightPlane.material.dispose();this.targetLine.geometry.dispose();this.targetLine.material.dispose()};"
                << std::endl ;
            out
                << "THREE.DirectionalLightHelper.prototype.update=function(){var a=new THREE.Vector3,b=new THREE.Vector3,c=new THREE.Vector3;return function(){a.setFromMatrixPosition(this.light.matrixWorld);b.setFromMatrixPosition(this.light.target.matrixWorld);c.subVectors(b,a);this.lightPlane.lookAt(c);this.lightPlane.material.color.copy(this.light.color).multiplyScalar(this.light.intensity);this.targetLine.geometry.vertices[1].copy(c);this.targetLine.geometry.verticesNeedUpdate=!0;this.targetLine.material.color.copy(this.lightPlane.material.color)}}();"
                << std::endl ;
            out
                << "THREE.EdgesHelper=function(a,b,c){b=void 0!==b?b:16777215;c=Math.cos(THREE.Math.degToRad(void 0!==c?c:1));var d=[0,0],e={},f=function(a,b){return a-b},g=[\"a\",\"b\",\"c\"],h=new THREE.BufferGeometry,k;a.geometry instanceof THREE.BufferGeometry?(k=new THREE.Geometry,k.fromBufferGeometry(a.geometry)):k=a.geometry.clone();k.mergeVertices();k.computeFaceNormals();var l=k.vertices;k=k.faces;for(var p=0,q=0,n=k.length;q<n;q++)for(var t=k[q],r=0;3>r;r++){d[0]=t[g[r]];d[1]=t[g[(r+1)%3]];d.sort(f);var s=d.toString();"
                << std::endl ;
            out
                << "void 0===e[s]?(e[s]={vert1:d[0],vert2:d[1],face1:q,face2:void 0},p++):e[s].face2=q}d=new Float32Array(6*p);f=0;for(s in e)if(g=e[s],void 0===g.face2||k[g.face1].normal.dot(k[g.face2].normal)<=c)p=l[g.vert1],d[f++]=p.x,d[f++]=p.y,d[f++]=p.z,p=l[g.vert2],d[f++]=p.x,d[f++]=p.y,d[f++]=p.z;h.addAttribute(\"position\",new THREE.BufferAttribute(d,3));THREE.Line.call(this,h,new THREE.LineBasicMaterial({color:b}),THREE.LinePieces);this.matrix=a.matrixWorld;this.matrixAutoUpdate=!1};"
                << std::endl ;
            out
                << "THREE.EdgesHelper.prototype=Object.create(THREE.Line.prototype);THREE.EdgesHelper.prototype.constructor=THREE.EdgesHelper;"
                << std::endl ;
            out
                << "THREE.FaceNormalsHelper=function(a,b,c,d){this.object=a;this.size=void 0!==b?b:1;a=void 0!==c?c:16776960;d=void 0!==d?d:1;b=new THREE.Geometry;c=0;for(var e=this.object.geometry.faces.length;c<e;c++)b.vertices.push(new THREE.Vector3,new THREE.Vector3);THREE.Line.call(this,b,new THREE.LineBasicMaterial({color:a,linewidth:d}),THREE.LinePieces);this.matrixAutoUpdate=!1;this.normalMatrix=new THREE.Matrix3;this.update()};THREE.FaceNormalsHelper.prototype=Object.create(THREE.Line.prototype);"
                << std::endl ;
            out
                << "THREE.FaceNormalsHelper.prototype.constructor=THREE.FaceNormalsHelper;"
                << std::endl ;
            out
                << "THREE.FaceNormalsHelper.prototype.update=function(){var a=this.geometry.vertices,b=this.object,c=b.geometry.vertices,d=b.geometry.faces,e=b.matrixWorld;b.updateMatrixWorld(!0);this.normalMatrix.getNormalMatrix(e);for(var f=b=0,g=d.length;b<g;b++,f+=2){var h=d[b];a[f].copy(c[h.a]).add(c[h.b]).add(c[h.c]).divideScalar(3).applyMatrix4(e);a[f+1].copy(h.normal).applyMatrix3(this.normalMatrix).normalize().multiplyScalar(this.size).add(a[f])}this.geometry.verticesNeedUpdate=!0;return this};"
                << std::endl ;
            out
                << "THREE.GridHelper=function(a,b){var c=new THREE.Geometry,d=new THREE.LineBasicMaterial({vertexColors:THREE.VertexColors});this.color1=new THREE.Color(4473924);this.color2=new THREE.Color(8947848);for(var e=-a;e<=a;e+=b){c.vertices.push(new THREE.Vector3(-a,0,e),new THREE.Vector3(a,0,e),new THREE.Vector3(e,0,-a),new THREE.Vector3(e,0,a));var f=0===e?this.color1:this.color2;c.colors.push(f,f,f,f)}THREE.Line.call(this,c,d,THREE.LinePieces)};THREE.GridHelper.prototype=Object.create(THREE.Line.prototype);"
                << std::endl ;
            out
                << "THREE.GridHelper.prototype.constructor=THREE.GridHelper;THREE.GridHelper.prototype.setColors=function(a,b){this.color1.set(a);this.color2.set(b);this.geometry.colorsNeedUpdate=!0};"
                << std::endl ;
            out
                << "THREE.HemisphereLightHelper=function(a,b){THREE.Object3D.call(this);this.light=a;this.light.updateMatrixWorld();this.matrix=a.matrixWorld;this.matrixAutoUpdate=!1;this.colors=[new THREE.Color,new THREE.Color];var c=new THREE.SphereGeometry(b,4,2);c.applyMatrix((new THREE.Matrix4).makeRotationX(-Math.PI/2));for(var d=0;8>d;d++)c.faces[d].color=this.colors[4>d?0:1];d=new THREE.MeshBasicMaterial({vertexColors:THREE.FaceColors,wireframe:!0});this.lightSphere=new THREE.Mesh(c,d);this.add(this.lightSphere);"
                << std::endl ;
            out
                << "this.update()};THREE.HemisphereLightHelper.prototype=Object.create(THREE.Object3D.prototype);THREE.HemisphereLightHelper.prototype.constructor=THREE.HemisphereLightHelper;THREE.HemisphereLightHelper.prototype.dispose=function(){this.lightSphere.geometry.dispose();this.lightSphere.material.dispose()};"
                << std::endl ;
            out
                << "THREE.HemisphereLightHelper.prototype.update=function(){var a=new THREE.Vector3;return function(){this.colors[0].copy(this.light.color).multiplyScalar(this.light.intensity);this.colors[1].copy(this.light.groundColor).multiplyScalar(this.light.intensity);this.lightSphere.lookAt(a.setFromMatrixPosition(this.light.matrixWorld).negate());this.lightSphere.geometry.colorsNeedUpdate=!0}}();"
                << std::endl ;
            out
                << "THREE.PointLightHelper=function(a,b){this.light=a;this.light.updateMatrixWorld();var c=new THREE.SphereGeometry(b,4,2),d=new THREE.MeshBasicMaterial({wireframe:!0,fog:!1});d.color.copy(this.light.color).multiplyScalar(this.light.intensity);THREE.Mesh.call(this,c,d);this.matrix=this.light.matrixWorld;this.matrixAutoUpdate=!1};THREE.PointLightHelper.prototype=Object.create(THREE.Mesh.prototype);THREE.PointLightHelper.prototype.constructor=THREE.PointLightHelper;"
                << std::endl ;
            out
                << "THREE.PointLightHelper.prototype.dispose=function(){this.geometry.dispose();this.material.dispose()};THREE.PointLightHelper.prototype.update=function(){this.material.color.copy(this.light.color).multiplyScalar(this.light.intensity)};"
                << std::endl ;
            out
                << "THREE.SkeletonHelper=function(a){this.bones=this.getBoneList(a);for(var b=new THREE.Geometry,c=0;c<this.bones.length;c++)this.bones[c].parent instanceof THREE.Bone&&(b.vertices.push(new THREE.Vector3),b.vertices.push(new THREE.Vector3),b.colors.push(new THREE.Color(0,0,1)),b.colors.push(new THREE.Color(0,1,0)));c=new THREE.LineBasicMaterial({vertexColors:THREE.VertexColors,depthTest:!1,depthWrite:!1,transparent:!0});THREE.Line.call(this,b,c,THREE.LinePieces);this.root=a;this.matrix=a.matrixWorld;"
                << std::endl ;
            out
                << "this.matrixAutoUpdate=!1;this.update()};THREE.SkeletonHelper.prototype=Object.create(THREE.Line.prototype);THREE.SkeletonHelper.prototype.constructor=THREE.SkeletonHelper;THREE.SkeletonHelper.prototype.getBoneList=function(a){var b=[];a instanceof THREE.Bone&&b.push(a);for(var c=0;c<a.children.length;c++)b.push.apply(b,this.getBoneList(a.children[c]));return b};"
                << std::endl ;
            out
                << "THREE.SkeletonHelper.prototype.update=function(){for(var a=this.geometry,b=(new THREE.Matrix4).getInverse(this.root.matrixWorld),c=new THREE.Matrix4,d=0,e=0;e<this.bones.length;e++){var f=this.bones[e];f.parent instanceof THREE.Bone&&(c.multiplyMatrices(b,f.matrixWorld),a.vertices[d].setFromMatrixPosition(c),c.multiplyMatrices(b,f.parent.matrixWorld),a.vertices[d+1].setFromMatrixPosition(c),d+=2)}a.verticesNeedUpdate=!0;a.computeBoundingSphere()};"
                << std::endl ;
            out
                << "THREE.SpotLightHelper=function(a){THREE.Object3D.call(this);this.light=a;this.light.updateMatrixWorld();this.matrix=a.matrixWorld;this.matrixAutoUpdate=!1;a=new THREE.CylinderGeometry(0,1,1,8,1,!0);a.applyMatrix((new THREE.Matrix4).makeTranslation(0,-.5,0));a.applyMatrix((new THREE.Matrix4).makeRotationX(-Math.PI/2));var b=new THREE.MeshBasicMaterial({wireframe:!0,fog:!1});this.cone=new THREE.Mesh(a,b);this.add(this.cone);this.update()};THREE.SpotLightHelper.prototype=Object.create(THREE.Object3D.prototype);"
                << std::endl ;
            out
                << "THREE.SpotLightHelper.prototype.constructor=THREE.SpotLightHelper;THREE.SpotLightHelper.prototype.dispose=function(){this.cone.geometry.dispose();this.cone.material.dispose()};"
                << std::endl ;
            out
                << "THREE.SpotLightHelper.prototype.update=function(){var a=new THREE.Vector3,b=new THREE.Vector3;return function(){var c=this.light.distance?this.light.distance:1E4,d=c*Math.tan(this.light.angle);this.cone.scale.set(d,d,c);a.setFromMatrixPosition(this.light.matrixWorld);b.setFromMatrixPosition(this.light.target.matrixWorld);this.cone.lookAt(b.sub(a));this.cone.material.color.copy(this.light.color).multiplyScalar(this.light.intensity)}}();"
                << std::endl ;
            out
                << "THREE.VertexNormalsHelper=function(a,b,c,d){this.object=a;this.size=void 0!==b?b:1;b=void 0!==c?c:16711680;d=void 0!==d?d:1;c=new THREE.Geometry;a=a.geometry.faces;for(var e=0,f=a.length;e<f;e++)for(var g=0,h=a[e].vertexNormals.length;g<h;g++)c.vertices.push(new THREE.Vector3,new THREE.Vector3);THREE.Line.call(this,c,new THREE.LineBasicMaterial({color:b,linewidth:d}),THREE.LinePieces);this.matrixAutoUpdate=!1;this.normalMatrix=new THREE.Matrix3;this.update()};THREE.VertexNormalsHelper.prototype=Object.create(THREE.Line.prototype);"
                << std::endl ;
            out
                << "THREE.VertexNormalsHelper.prototype.constructor=THREE.VertexNormalsHelper;"
                << std::endl ;
            out
                << "THREE.VertexNormalsHelper.prototype.update=function(a){var b=new THREE.Vector3;return function(a){a=[\"a\",\"b\",\"c\",\"d\"];this.object.updateMatrixWorld(!0);this.normalMatrix.getNormalMatrix(this.object.matrixWorld);for(var d=this.geometry.vertices,e=this.object.geometry.vertices,f=this.object.geometry.faces,g=this.object.matrixWorld,h=0,k=0,l=f.length;k<l;k++)for(var p=f[k],q=0,n=p.vertexNormals.length;q<n;q++){var t=p.vertexNormals[q];d[h].copy(e[p[a[q]]]).applyMatrix4(g);b.copy(t).applyMatrix3(this.normalMatrix).normalize().multiplyScalar(this.size);"
                << std::endl ;
            out
                << "b.add(d[h]);h+=1;d[h].copy(b);h+=1}this.geometry.verticesNeedUpdate=!0;return this}}();"
                << std::endl ;
            out
                << "THREE.VertexTangentsHelper=function(a,b,c,d){this.object=a;this.size=void 0!==b?b:1;b=void 0!==c?c:255;d=void 0!==d?d:1;c=new THREE.Geometry;a=a.geometry.faces;for(var e=0,f=a.length;e<f;e++)for(var g=0,h=a[e].vertexTangents.length;g<h;g++)c.vertices.push(new THREE.Vector3),c.vertices.push(new THREE.Vector3);THREE.Line.call(this,c,new THREE.LineBasicMaterial({color:b,linewidth:d}),THREE.LinePieces);this.matrixAutoUpdate=!1;this.update()};THREE.VertexTangentsHelper.prototype=Object.create(THREE.Line.prototype);"
                << std::endl ;
            out
                << "THREE.VertexTangentsHelper.prototype.constructor=THREE.VertexTangentsHelper;"
                << std::endl ;
            out
                << "THREE.VertexTangentsHelper.prototype.update=function(a){var b=new THREE.Vector3;return function(a){a=[\"a\",\"b\",\"c\",\"d\"];this.object.updateMatrixWorld(!0);for(var d=this.geometry.vertices,e=this.object.geometry.vertices,f=this.object.geometry.faces,g=this.object.matrixWorld,h=0,k=0,l=f.length;k<l;k++)for(var p=f[k],q=0,n=p.vertexTangents.length;q<n;q++){var t=p.vertexTangents[q];d[h].copy(e[p[a[q]]]).applyMatrix4(g);b.copy(t).transformDirection(g).multiplyScalar(this.size);b.add(d[h]);h+=1;d[h].copy(b);"
                << std::endl ;
            out << "h+=1}this.geometry.verticesNeedUpdate=!0;return this}}();"
                << std::endl ;
            out
                << "THREE.WireframeHelper=function(a,b){var c=void 0!==b?b:16777215,d=[0,0],e={},f=function(a,b){return a-b},g=[\"a\",\"b\",\"c\"],h=new THREE.BufferGeometry;if(a.geometry instanceof THREE.Geometry){for(var k=a.geometry.vertices,l=a.geometry.faces,p=0,q=new Uint32Array(6*l.length),n=0,t=l.length;n<t;n++)for(var r=l[n],s=0;3>s;s++){d[0]=r[g[s]];d[1]=r[g[(s+1)%3]];d.sort(f);var u=d.toString();void 0===e[u]&&(q[2*p]=d[0],q[2*p+1]=d[1],e[u]=!0,p++)}d=new Float32Array(6*p);n=0;for(t=p;n<t;n++)for(s=0;2>s;s++)p="
                << std::endl ;
            out
                << "k[q[2*n+s]],g=6*n+3*s,d[g+0]=p.x,d[g+1]=p.y,d[g+2]=p.z;h.addAttribute(\"position\",new THREE.BufferAttribute(d,3))}else if(a.geometry instanceof THREE.BufferGeometry){if(void 0!==a.geometry.attributes.index){k=a.geometry.attributes.position.array;t=a.geometry.attributes.index.array;l=a.geometry.drawcalls;p=0;0===l.length&&(l=[{count:t.length,index:0,start:0}]);for(var q=new Uint32Array(2*t.length),r=0,v=l.length;r<v;++r)for(var s=l[r].start,u=l[r].count,g=l[r].index,n=s,x=s+u;n<x;n+=3)for(s=0;3>s;s++)d[0]="
                << std::endl ;
            out
                << "g+t[n+s],d[1]=g+t[n+(s+1)%3],d.sort(f),u=d.toString(),void 0===e[u]&&(q[2*p]=d[0],q[2*p+1]=d[1],e[u]=!0,p++);d=new Float32Array(6*p);n=0;for(t=p;n<t;n++)for(s=0;2>s;s++)g=6*n+3*s,p=3*q[2*n+s],d[g+0]=k[p],d[g+1]=k[p+1],d[g+2]=k[p+2]}else for(k=a.geometry.attributes.position.array,p=k.length/3,q=p/3,d=new Float32Array(6*p),n=0,t=q;n<t;n++)for(s=0;3>s;s++)g=18*n+6*s,q=9*n+3*s,d[g+0]=k[q],d[g+1]=k[q+1],d[g+2]=k[q+2],p=9*n+(s+1)%3*3,d[g+3]=k[p],d[g+4]=k[p+1],d[g+5]=k[p+2];h.addAttribute(\"position\",new THREE.BufferAttribute(d,"
                << std::endl ;
            out
                << "3))}THREE.Line.call(this,h,new THREE.LineBasicMaterial({color:c}),THREE.LinePieces);this.matrix=a.matrixWorld;this.matrixAutoUpdate=!1};THREE.WireframeHelper.prototype=Object.create(THREE.Line.prototype);THREE.WireframeHelper.prototype.constructor=THREE.WireframeHelper;THREE.ImmediateRenderObject=function(){THREE.Object3D.call(this);this.render=function(a){}};THREE.ImmediateRenderObject.prototype=Object.create(THREE.Object3D.prototype);THREE.ImmediateRenderObject.prototype.constructor=THREE.ImmediateRenderObject;"
                << std::endl ;
            out
                << "THREE.MorphBlendMesh=function(a,b){THREE.Mesh.call(this,a,b);this.animationsMap={};this.animationsList=[];var c=this.geometry.morphTargets.length;this.createAnimation(\"__default\",0,c-1,c/1);this.setAnimationWeight(\"__default\",1)};THREE.MorphBlendMesh.prototype=Object.create(THREE.Mesh.prototype);THREE.MorphBlendMesh.prototype.constructor=THREE.MorphBlendMesh;"
                << std::endl ;
            out
                << "THREE.MorphBlendMesh.prototype.createAnimation=function(a,b,c,d){b={startFrame:b,endFrame:c,length:c-b+1,fps:d,duration:(c-b)/d,lastFrame:0,currentFrame:0,active:!1,time:0,direction:1,weight:1,directionBackwards:!1,mirroredLoop:!1};this.animationsMap[a]=b;this.animationsList.push(b)};"
                << std::endl ;
            out
                << "THREE.MorphBlendMesh.prototype.autoCreateAnimations=function(a){for(var b=/([a-z]+)_?(\\d+)/,c,d={},e=this.geometry,f=0,g=e.morphTargets.length;f<g;f++){var h=e.morphTargets[f].name.match(b);if(h&&1<h.length){var k=h[1];d[k]||(d[k]={start:Infinity,end:-Infinity});h=d[k];f<h.start&&(h.start=f);f>h.end&&(h.end=f);c||(c=k)}}for(k in d)h=d[k],this.createAnimation(k,h.start,h.end,a);this.firstAnimation=c};"
                << std::endl ;
            out
                << "THREE.MorphBlendMesh.prototype.setAnimationDirectionForward=function(a){if(a=this.animationsMap[a])a.direction=1,a.directionBackwards=!1};THREE.MorphBlendMesh.prototype.setAnimationDirectionBackward=function(a){if(a=this.animationsMap[a])a.direction=-1,a.directionBackwards=!0};THREE.MorphBlendMesh.prototype.setAnimationFPS=function(a,b){var c=this.animationsMap[a];c&&(c.fps=b,c.duration=(c.end-c.start)/c.fps)};"
                << std::endl ;
            out
                << "THREE.MorphBlendMesh.prototype.setAnimationDuration=function(a,b){var c=this.animationsMap[a];c&&(c.duration=b,c.fps=(c.end-c.start)/c.duration)};THREE.MorphBlendMesh.prototype.setAnimationWeight=function(a,b){var c=this.animationsMap[a];c&&(c.weight=b)};THREE.MorphBlendMesh.prototype.setAnimationTime=function(a,b){var c=this.animationsMap[a];c&&(c.time=b)};THREE.MorphBlendMesh.prototype.getAnimationTime=function(a){var b=0;if(a=this.animationsMap[a])b=a.time;return b};"
                << std::endl ;
            out
                << "THREE.MorphBlendMesh.prototype.getAnimationDuration=function(a){var b=-1;if(a=this.animationsMap[a])b=a.duration;return b};THREE.MorphBlendMesh.prototype.playAnimation=function(a){var b=this.animationsMap[a];b?(b.time=0,b.active=!0):THREE.warn(\"THREE.MorphBlendMesh: animation[\"+a+\"] undefined in .playAnimation()\")};THREE.MorphBlendMesh.prototype.stopAnimation=function(a){if(a=this.animationsMap[a])a.active=!1};"
                << std::endl ;
            out
                << "THREE.MorphBlendMesh.prototype.update=function(a){for(var b=0,c=this.animationsList.length;b<c;b++){var d=this.animationsList[b];if(d.active){var e=d.duration/d.length;d.time+=d.direction*a;if(d.mirroredLoop){if(d.time>d.duration||0>d.time)d.direction*=-1,d.time>d.duration&&(d.time=d.duration,d.directionBackwards=!0),0>d.time&&(d.time=0,d.directionBackwards=!1)}else d.time%=d.duration,0>d.time&&(d.time+=d.duration);var f=d.startFrame+THREE.Math.clamp(Math.floor(d.time/e),0,d.length-1),g=d.weight;"
                << std::endl ;
            out
                << "f!==d.currentFrame&&(this.morphTargetInfluences[d.lastFrame]=0,this.morphTargetInfluences[d.currentFrame]=1*g,this.morphTargetInfluences[f]=0,d.lastFrame=d.currentFrame,d.currentFrame=f);e=d.time%e/e;d.directionBackwards&&(e=1-e);this.morphTargetInfluences[d.currentFrame]=e*g;this.morphTargetInfluences[d.lastFrame]=(1-e)*g}}};"
                << std::endl ;
        }

        void print_TrackballControls( std::ofstream& out )
        {
            out << "/**" << std::endl ;
            out << " * @author Eberhard Graether / http://egraether.com/"
                << std::endl ;
            out << " * @author Mark Lundin  / http://mark-lundin.com" << std::endl ;
            out << " * @author Simone Manini / http://daron1337.github.io"
                << std::endl ;
            out << " * @author Luca Antiga  / http://lantiga.github.io"
                << std::endl ;
            out << " */" << std::endl ;
            out << "" << std::endl ;
            out << "THREE.TrackballControls = function ( object, domElement ) {"
                << std::endl ;
            out << "" << std::endl ;
            out << "    var _this = this;" << std::endl ;
            out
                << "    var STATE = { NONE: -1, ROTATE: 0, ZOOM: 1, PAN: 2, TOUCH_ROTATE: 3, TOUCH_ZOOM_PAN: 4 };"
                << std::endl ;
            out << "" << std::endl ;
            out << "    this.object = object;" << std::endl ;
            out
                << "    this.domElement = ( domElement !== undefined ) ? domElement : document;"
                << std::endl ;
            out << "" << std::endl ;
            out << "    // API" << std::endl ;
            out << "" << std::endl ;
            out << "    this.enabled = true;" << std::endl ;
            out << "" << std::endl ;
            out << "    this.screen = { left: 0, top: 0, width: 0, height: 0 };"
                << std::endl ;
            out << "" << std::endl ;
            out << "    this.rotateSpeed = 1.0;" << std::endl ;
            out << "    this.zoomSpeed = 1.2;" << std::endl ;
            out << "    this.panSpeed = 0.3;" << std::endl ;
            out << "" << std::endl ;
            out << "    this.noRotate = false;" << std::endl ;
            out << "    this.noZoom = false;" << std::endl ;
            out << "    this.noPan = false;" << std::endl ;
            out << "" << std::endl ;
            out << "    this.staticMoving = false;" << std::endl ;
            out << "    this.dynamicDampingFactor = 0.2;" << std::endl ;
            out << "" << std::endl ;
            out << "    this.minDistance = 0;" << std::endl ;
            out << "    this.maxDistance = Infinity;" << std::endl ;
            out << "" << std::endl ;
            out << "    this.keys = [ 65 /*A*/, 83 /*S*/, 68 /*D*/ ];" << std::endl ;
            out << "" << std::endl ;
            out << "    // internals" << std::endl ;
            out << "" << std::endl ;
            out << "    this.target = new THREE.Vector3();" << std::endl ;
            out << "" << std::endl ;
            out << "    var EPS = 0.000001;" << std::endl ;
            out << "" << std::endl ;
            out << "    var lastPosition = new THREE.Vector3();" << std::endl ;
            out << "" << std::endl ;
            out << "    var _state = STATE.NONE," << std::endl ;
            out << "    _prevState = STATE.NONE," << std::endl ;
            out << "" << std::endl ;
            out << "    _eye = new THREE.Vector3()," << std::endl ;
            out << "" << std::endl ;
            out << "    _movePrev = new THREE.Vector2()," << std::endl ;
            out << "    _moveCurr = new THREE.Vector2()," << std::endl ;
            out << "" << std::endl ;
            out << "    _lastAxis = new THREE.Vector3()," << std::endl ;
            out << "    _lastAngle = 0," << std::endl ;
            out << "" << std::endl ;
            out << "    _zoomStart = new THREE.Vector2()," << std::endl ;
            out << "    _zoomEnd = new THREE.Vector2()," << std::endl ;
            out << "" << std::endl ;
            out << "    _touchZoomDistanceStart = 0," << std::endl ;
            out << "    _touchZoomDistanceEnd = 0," << std::endl ;
            out << "" << std::endl ;
            out << "    _panStart = new THREE.Vector2()," << std::endl ;
            out << "    _panEnd = new THREE.Vector2();" << std::endl ;
            out << "" << std::endl ;
            out << "    //TESt" << std::endl ;
            out << "    _hasChange = false ;" << std::endl ;
            out << "" << std::endl ;
            out << "    // for reset" << std::endl ;
            out << "" << std::endl ;
            out << "    this.target0 = this.target.clone();" << std::endl ;
            out << "    this.position0 = this.object.position.clone();"
                << std::endl ;
            out << "    this.up0 = this.object.up.clone();" << std::endl ;
            out << "" << std::endl ;
            out << "    // events" << std::endl ;
            out << "" << std::endl ;
            out << "    var changeEvent = { type: 'change' };" << std::endl ;
            out << "    var startEvent = { type: 'start' };" << std::endl ;
            out << "    var endEvent = { type: 'end' };" << std::endl ;
            out << "" << std::endl ;
            out << "" << std::endl ;
            out << "    // methods" << std::endl ;
            out << "" << std::endl ;
            out << "    this.handleResize = function () {" << std::endl ;
            out << "" << std::endl ;
            out << "        if ( this.domElement === document ) {" << std::endl ;
            out << "" << std::endl ;
            out << "            this.screen.left = 0;" << std::endl ;
            out << "            this.screen.top = 0;" << std::endl ;
            out << "            this.screen.width = window.innerWidth;"
                << std::endl ;
            out << "            this.screen.height = window.innerHeight;"
                << std::endl ;
            out << "" << std::endl ;
            out << "        } else {" << std::endl ;
            out << "" << std::endl ;
            out << "            var box = this.domElement.getBoundingClientRect();"
                << std::endl ;
            out
                << "            // adjustments come from similar code in the jquery offset() function"
                << std::endl ;
            out
                << "            var d = this.domElement.ownerDocument.documentElement;"
                << std::endl ;
            out
                << "            this.screen.left = box.left + window.pageXOffset - d.clientLeft;"
                << std::endl ;
            out
                << "            this.screen.top = box.top + window.pageYOffset - d.clientTop;"
                << std::endl ;
            out << "            this.screen.width = box.width;" << std::endl ;
            out << "            this.screen.height = box.height;" << std::endl ;
            out << "" << std::endl ;
            out << "        }" << std::endl ;
            out << "" << std::endl ;
            out << "    };" << std::endl ;
            out << "" << std::endl ;
            out << "    this.handleEvent = function ( event ) {" << std::endl ;
            out << "" << std::endl ;
            out << "        if ( typeof this[ event.type ] == 'function' ) {"
                << std::endl ;
            out << "" << std::endl ;
            out << "            this[ event.type ]( event );" << std::endl ;
            out << "" << std::endl ;
            out << "        }" << std::endl ;
            out << "" << std::endl ;
            out << "    };" << std::endl ;
            out << "" << std::endl ;
            out << "    var getMouseOnScreen = ( function () {" << std::endl ;
            out << "" << std::endl ;
            out << "        var vector = new THREE.Vector2();" << std::endl ;
            out << "" << std::endl ;
            out << "        return function ( pageX, pageY ) {" << std::endl ;
            out << "" << std::endl ;
            out << "            vector.set(" << std::endl ;
            out
                << "                ( pageX - _this.screen.left ) / _this.screen.width,"
                << std::endl ;
            out
                << "                ( pageY - _this.screen.top ) / _this.screen.height"
                << std::endl ;
            out << "            );" << std::endl ;
            out << "" << std::endl ;
            out << "            return vector;" << std::endl ;
            out << "" << std::endl ;
            out << "        };" << std::endl ;
            out << "" << std::endl ;
            out << "    }() );" << std::endl ;
            out << "" << std::endl ;
            out << "    var getMouseOnCircle = ( function () {" << std::endl ;
            out << "" << std::endl ;
            out << "        var vector = new THREE.Vector2();" << std::endl ;
            out << "" << std::endl ;
            out << "        return function ( pageX, pageY ) {" << std::endl ;
            out << "" << std::endl ;
            out << "            vector.set(" << std::endl ;
            out
                << "                ( ( pageX - _this.screen.width * 0.5 - _this.screen.left ) / ( _this.screen.width * 0.5 ) ),"
                << std::endl ;
            out
                << "                ( ( _this.screen.height + 2 * ( _this.screen.top - pageY ) ) / _this.screen.width ) // screen.width intentional"
                << std::endl ;
            out << "            );" << std::endl ;
            out << "" << std::endl ;
            out << "            return vector;" << std::endl ;
            out << "        };" << std::endl ;
            out << "" << std::endl ;
            out << "    }() );" << std::endl ;
            out << "" << std::endl ;
            out << "    this.rotateCamera = (function() {" << std::endl ;
            out << "" << std::endl ;
            out << "        var axis = new THREE.Vector3()," << std::endl ;
            out << "            quaternion = new THREE.Quaternion()," << std::endl ;
            out << "            eyeDirection = new THREE.Vector3()," << std::endl ;
            out << "            objectUpDirection = new THREE.Vector3(),"
                << std::endl ;
            out << "            objectSidewaysDirection = new THREE.Vector3(),"
                << std::endl ;
            out << "            moveDirection = new THREE.Vector3()," << std::endl ;
            out << "            angle;" << std::endl ;
            out << "" << std::endl ;
            out << "        return function () {" << std::endl ;
            out << "" << std::endl ;
            out
                << "            moveDirection.set( _moveCurr.x - _movePrev.x, _moveCurr.y - _movePrev.y, 0 );"
                << std::endl ;
            out << "            angle = moveDirection.length();" << std::endl ;
            out << "" << std::endl ;
            out << "            if ( angle ) {" << std::endl ;
            out << "" << std::endl ;
            out
                << "                _eye.copy( _this.object.position ).sub( _this.target );"
                << std::endl ;
            out << "" << std::endl ;
            out << "                eyeDirection.copy( _eye ).normalize();"
                << std::endl ;
            out
                << "                objectUpDirection.copy( _this.object.up ).normalize();"
                << std::endl ;
            out
                << "                objectSidewaysDirection.crossVectors( objectUpDirection, eyeDirection ).normalize();"
                << std::endl ;
            out << "" << std::endl ;
            out
                << "                objectUpDirection.setLength( _moveCurr.y - _movePrev.y );"
                << std::endl ;
            out
                << "                objectSidewaysDirection.setLength( _moveCurr.x - _movePrev.x );"
                << std::endl ;
            out << "" << std::endl ;
            out
                << "                moveDirection.copy( objectUpDirection.add( objectSidewaysDirection ) );"
                << std::endl ;
            out << "" << std::endl ;
            out
                << "                axis.crossVectors( moveDirection, _eye ).normalize();"
                << std::endl ;
            out << "" << std::endl ;
            out << "                angle *= _this.rotateSpeed;" << std::endl ;
            out << "                quaternion.setFromAxisAngle( axis, angle );"
                << std::endl ;
            out << "" << std::endl ;
            out << "                _eye.applyQuaternion( quaternion );"
                << std::endl ;
            out << "                _this.object.up.applyQuaternion( quaternion );"
                << std::endl ;
            out << "" << std::endl ;
            out << "                _lastAxis.copy( axis );" << std::endl ;
            out << "                _lastAngle = angle;" << std::endl ;
            out << "" << std::endl ;
            out << "            }" << std::endl ;
            out << "" << std::endl ;
            out << "            else if ( !_this.staticMoving && _lastAngle ) {"
                << std::endl ;
            out << "" << std::endl ;
            out
                << "                _lastAngle *= Math.sqrt( 1.0 - _this.dynamicDampingFactor );"
                << std::endl ;
            out
                << "                _eye.copy( _this.object.position ).sub( _this.target );"
                << std::endl ;
            out
                << "                quaternion.setFromAxisAngle( _lastAxis, _lastAngle );"
                << std::endl ;
            out << "                _eye.applyQuaternion( quaternion );"
                << std::endl ;
            out << "                _this.object.up.applyQuaternion( quaternion );"
                << std::endl ;
            out << "" << std::endl ;
            out << "            }" << std::endl ;
            out << "" << std::endl ;
            out << "            _movePrev.copy( _moveCurr );" << std::endl ;
            out << "" << std::endl ;
            out << "        };" << std::endl ;
            out << "" << std::endl ;
            out << "    }());" << std::endl ;
            out << "" << std::endl ;
            out << "" << std::endl ;
            out << "    this.zoomCamera = function () {" << std::endl ;
            out << "" << std::endl ;
            out << "        var factor;" << std::endl ;
            out << "" << std::endl ;
            out << "        if ( _state === STATE.TOUCH_ZOOM_PAN ) {" << std::endl ;
            out << "" << std::endl ;
            out
                << "            factor = _touchZoomDistanceStart / _touchZoomDistanceEnd;"
                << std::endl ;
            out << "            _touchZoomDistanceStart = _touchZoomDistanceEnd;"
                << std::endl ;
            out << "            _eye.multiplyScalar( factor );" << std::endl ;
            out << "" << std::endl ;
            out << "        } else {" << std::endl ;
            out << "" << std::endl ;
            out
                << "            factor = 1.0 + ( _zoomEnd.y - _zoomStart.y ) * _this.zoomSpeed;"
                << std::endl ;
            out << "" << std::endl ;
            out << "            if ( factor !== 1.0 && factor > 0.0 ) {"
                << std::endl ;
            out << "" << std::endl ;
            out << "                _eye.multiplyScalar( factor );" << std::endl ;
            out << "" << std::endl ;
            out << "                if ( _this.staticMoving ) {" << std::endl ;
            out << "" << std::endl ;
            out << "                    _zoomStart.copy( _zoomEnd );" << std::endl ;
            out << "" << std::endl ;
            out << "                } else {" << std::endl ;
            out << "" << std::endl ;
            out
                << "                    _zoomStart.y += ( _zoomEnd.y - _zoomStart.y ) * this.dynamicDampingFactor;"
                << std::endl ;
            out << "" << std::endl ;
            out << "                }" << std::endl ;
            out << "" << std::endl ;
            out << "            }" << std::endl ;
            out << "" << std::endl ;
            out << "        }" << std::endl ;
            out << "" << std::endl ;
            out << "    };" << std::endl ;
            out << "" << std::endl ;
            out << "    this.panCamera = (function() {" << std::endl ;
            out << "" << std::endl ;
            out << "        var mouseChange = new THREE.Vector2()," << std::endl ;
            out << "            objectUp = new THREE.Vector3()," << std::endl ;
            out << "            pan = new THREE.Vector3();" << std::endl ;
            out << "" << std::endl ;
            out << "        return function () {" << std::endl ;
            out << "" << std::endl ;
            out << "            mouseChange.copy( _panEnd ).sub( _panStart );"
                << std::endl ;
            out << "" << std::endl ;
            out << "            if ( mouseChange.lengthSq() ) {" << std::endl ;
            out << "" << std::endl ;
            out
                << "                mouseChange.multiplyScalar( _eye.length() * _this.panSpeed );"
                << std::endl ;
            out << "" << std::endl ;
            out
                << "                pan.copy( _eye ).cross( _this.object.up ).setLength( mouseChange.x );"
                << std::endl ;
            out
                << "                pan.add( objectUp.copy( _this.object.up ).setLength( mouseChange.y ) );"
                << std::endl ;
            out << "" << std::endl ;
            out << "                _this.object.position.add( pan );" << std::endl ;
            out << "                _this.target.add( pan );" << std::endl ;
            out << "" << std::endl ;
            out << "                if ( _this.staticMoving ) {" << std::endl ;
            out << "" << std::endl ;
            out << "                    _panStart.copy( _panEnd );" << std::endl ;
            out << "" << std::endl ;
            out << "                } else {" << std::endl ;
            out << "" << std::endl ;
            out
                << "                    _panStart.add( mouseChange.subVectors( _panEnd, _panStart ).multiplyScalar( _this.dynamicDampingFactor ) );"
                << std::endl ;
            out << "" << std::endl ;
            out << "                }" << std::endl ;
            out << "" << std::endl ;
            out << "            }" << std::endl ;
            out << "        };" << std::endl ;
            out << "" << std::endl ;
            out << "    }());" << std::endl ;
            out << "" << std::endl ;
            out << "    this.checkDistances = function () {" << std::endl ;
            out << "" << std::endl ;
            out << "        if ( !_this.noZoom || !_this.noPan ) {" << std::endl ;
            out << "" << std::endl ;
            out
                << "            if ( _eye.lengthSq() > _this.maxDistance * _this.maxDistance ) {"
                << std::endl ;
            out << "" << std::endl ;
            out
                << "                _this.object.position.addVectors( _this.target, _eye.setLength( _this.maxDistance ) );"
                << std::endl ;
            out << "" << std::endl ;
            out << "            }" << std::endl ;
            out << "" << std::endl ;
            out
                << "            if ( _eye.lengthSq() < _this.minDistance * _this.minDistance ) {"
                << std::endl ;
            out << "" << std::endl ;
            out
                << "                _this.object.position.addVectors( _this.target, _eye.setLength( _this.minDistance ) );"
                << std::endl ;
            out << "" << std::endl ;
            out << "            }" << std::endl ;
            out << "" << std::endl ;
            out << "        }" << std::endl ;
            out << "" << std::endl ;
            out << "    };" << std::endl ;
            out << "" << std::endl ;
            out << "    this.update = function () {" << std::endl ;
            out << "" << std::endl ;
            out << "        _eye.subVectors( _this.object.position, _this.target );"
                << std::endl ;
            out << "" << std::endl ;
            out << "        if ( !_this.noRotate ) {" << std::endl ;
            out << "" << std::endl ;
            out << "            _this.rotateCamera();" << std::endl ;
            out << "" << std::endl ;
            out << "        }" << std::endl ;
            out << "" << std::endl ;
            out << "        if ( !_this.noZoom ) {" << std::endl ;
            out << "" << std::endl ;
            out << "            _this.zoomCamera();" << std::endl ;
            out << "" << std::endl ;
            out << "        }" << std::endl ;
            out << "" << std::endl ;
            out << "        if ( !_this.noPan ) {" << std::endl ;
            out << "" << std::endl ;
            out << "            _this.panCamera();" << std::endl ;
            out << "" << std::endl ;
            out << "        }" << std::endl ;
            out << "" << std::endl ;
            out << "        _this.object.position.addVectors( _this.target, _eye );"
                << std::endl ;
            out << "" << std::endl ;
            out << "        _this.checkDistances();" << std::endl ;
            out << "" << std::endl ;
            out << "        _this.object.lookAt( _this.target );" << std::endl ;
            out << "" << std::endl ;
            out
                << "        if ( lastPosition.distanceToSquared( _this.object.position ) > EPS ) {"
                << std::endl ;
            out << "" << std::endl ;
            out << "            _this.dispatchEvent( changeEvent );" << std::endl ;
            out << "" << std::endl ;
            out << "            lastPosition.copy( _this.object.position );"
                << std::endl ;
            out << "" << std::endl ;
            out << "        }" << std::endl ;
            out << "" << std::endl ;
            out << "    };" << std::endl ;
            out << "" << std::endl ;
            out << "    this.reset = function () {" << std::endl ;
            out << "" << std::endl ;
            out << "        _state = STATE.NONE;" << std::endl ;
            out << "        _prevState = STATE.NONE;" << std::endl ;
            out << "" << std::endl ;
            out << "        _this.target.copy( _this.target0 );" << std::endl ;
            out << "        _this.object.position.copy( _this.position0 );"
                << std::endl ;
            out << "        _this.object.up.copy( _this.up0 );" << std::endl ;
            out << "" << std::endl ;
            out << "        _eye.subVectors( _this.object.position, _this.target );"
                << std::endl ;
            out << "" << std::endl ;
            out << "        _this.object.lookAt( _this.target );" << std::endl ;
            out << "" << std::endl ;
            out << "        _this.dispatchEvent( changeEvent );" << std::endl ;
            out << "" << std::endl ;
            out << "        lastPosition.copy( _this.object.position );"
                << std::endl ;
            out << "" << std::endl ;
            out << "    };" << std::endl ;
            out << "" << std::endl ;
            out << "    // listeners" << std::endl ;
            out << "" << std::endl ;
            out << "    function keydown( event ) {" << std::endl ;
            out << "" << std::endl ;
            out << "        if ( _this.enabled === false ) return;" << std::endl ;
            out << "" << std::endl ;
            out << "        window.removeEventListener( 'keydown', keydown );"
                << std::endl ;
            out << "" << std::endl ;
            out << "        _prevState = _state;" << std::endl ;
            out << "" << std::endl ;
            out << "        if ( _state !== STATE.NONE ) {" << std::endl ;
            out << "" << std::endl ;
            out << "            return;" << std::endl ;
            out << "" << std::endl ;
            out
                << "        } else if ( event.keyCode === _this.keys[ STATE.ROTATE ] && !_this.noRotate ) {"
                << std::endl ;
            out << "" << std::endl ;
            out << "            _state = STATE.ROTATE;" << std::endl ;
            out << "" << std::endl ;
            out
                << "        } else if ( event.keyCode === _this.keys[ STATE.ZOOM ] && !_this.noZoom ) {"
                << std::endl ;
            out << "" << std::endl ;
            out << "            _state = STATE.ZOOM;" << std::endl ;
            out << "" << std::endl ;
            out
                << "        } else if ( event.keyCode === _this.keys[ STATE.PAN ] && !_this.noPan ) {"
                << std::endl ;
            out << "" << std::endl ;
            out << "            _state = STATE.PAN;" << std::endl ;
            out << "" << std::endl ;
            out << "        }" << std::endl ;
            out << "" << std::endl ;
            out << "    }" << std::endl ;
            out << "" << std::endl ;
            out << "    function keyup( event ) {" << std::endl ;
            out << "" << std::endl ;
            out << "        if ( _this.enabled === false ) return;" << std::endl ;
            out << "" << std::endl ;
            out << "        _state = _prevState;" << std::endl ;
            out << "" << std::endl ;
            out << "        window.addEventListener( 'keydown', keydown, false );"
                << std::endl ;
            out << "" << std::endl ;
            out << "    }" << std::endl ;
            out << "" << std::endl ;
            out << "    function mousedown( event ) {" << std::endl ;
            out << "" << std::endl ;
            out << "        if ( _this.enabled === false ) return;" << std::endl ;
            out << "" << std::endl ;
            out << "        event.preventDefault();" << std::endl ;
            out << "        event.stopPropagation();" << std::endl ;
            out << "" << std::endl ;
            out << "        if ( _state === STATE.NONE ) {" << std::endl ;
            out << "" << std::endl ;
            out << "            _state = event.button;" << std::endl ;
            out << "" << std::endl ;
            out << "        }" << std::endl ;
            out << "" << std::endl ;
            out << "        if ( _state === STATE.ROTATE && !_this.noRotate ) {"
                << std::endl ;
            out << "" << std::endl ;
            out
                << "            _moveCurr.copy( getMouseOnCircle( event.pageX, event.pageY ) );"
                << std::endl ;
            out << "            _movePrev.copy(_moveCurr);" << std::endl ;
            out << "" << std::endl ;
            out << "        } else if ( _state === STATE.ZOOM && !_this.noZoom ) {"
                << std::endl ;
            out << "" << std::endl ;
            out
                << "            _zoomStart.copy( getMouseOnScreen( event.pageX, event.pageY ) );"
                << std::endl ;
            out << "            _zoomEnd.copy(_zoomStart);" << std::endl ;
            out << "" << std::endl ;
            out << "        } else if ( _state === STATE.PAN && !_this.noPan ) {"
                << std::endl ;
            out << "" << std::endl ;
            out
                << "            _panStart.copy( getMouseOnScreen( event.pageX, event.pageY ) );"
                << std::endl ;
            out << "            _panEnd.copy(_panStart);" << std::endl ;
            out << "" << std::endl ;
            out << "        }" << std::endl ;
            out << "" << std::endl ;
            out
                << "        document.addEventListener( 'mousemove', mousemove, false );"
                << std::endl ;
            out << "        document.addEventListener( 'mouseup', mouseup, false );"
                << std::endl ;
            out << "" << std::endl ;
            out << "        _this.dispatchEvent( startEvent );" << std::endl ;
            out << "" << std::endl ;
            out << "    }" << std::endl ;
            out << "" << std::endl ;
            out << "    function mousemove( event ) {" << std::endl ;
            out << "" << std::endl ;
            out << "        if ( _this.enabled === false ) return;" << std::endl ;
            out << "" << std::endl ;
            out << "        event.preventDefault();" << std::endl ;
            out << "        event.stopPropagation();" << std::endl ;
            out << "" << std::endl ;
            out << "        if ( _state === STATE.ROTATE && !_this.noRotate ) {"
                << std::endl ;
            out << "" << std::endl ;
            out << "            _movePrev.copy(_moveCurr);" << std::endl ;
            out
                << "            _moveCurr.copy( getMouseOnCircle( event.pageX, event.pageY ) );"
                << std::endl ;
            out << "" << std::endl ;
            out << "        } else if ( _state === STATE.ZOOM && !_this.noZoom ) {"
                << std::endl ;
            out << "" << std::endl ;
            out
                << "            _zoomEnd.copy( getMouseOnScreen( event.pageX, event.pageY ) );"
                << std::endl ;
            out << "" << std::endl ;
            out << "        } else if ( _state === STATE.PAN && !_this.noPan ) {"
                << std::endl ;
            out << "" << std::endl ;
            out
                << "            _panEnd.copy( getMouseOnScreen( event.pageX, event.pageY ) );"
                << std::endl ;
            out << "" << std::endl ;
            out << "        }" << std::endl ;
            out << "" << std::endl ;
            out << "    }" << std::endl ;
            out << "" << std::endl ;
            out << "    function mouseup( event ) {" << std::endl ;
            out << "" << std::endl ;
            out << "        if ( _this.enabled === false ) return;" << std::endl ;
            out << "" << std::endl ;
            out << "        event.preventDefault();" << std::endl ;
            out << "        event.stopPropagation();" << std::endl ;
            out << "" << std::endl ;
            out << "        _state = STATE.NONE;" << std::endl ;
            out << "" << std::endl ;
            out << "        document.removeEventListener( 'mousemove', mousemove );"
                << std::endl ;
            out << "        document.removeEventListener( 'mouseup', mouseup );"
                << std::endl ;
            out << "        _this.dispatchEvent( endEvent );" << std::endl ;
            out << "" << std::endl ;
            out << "    }" << std::endl ;
            out << "" << std::endl ;
            out << "    function mousewheel( event ) {" << std::endl ;
            out << "" << std::endl ;
            out << "        if ( _this.enabled === false ) return;" << std::endl ;
            out << "" << std::endl ;
            out << "        event.preventDefault();" << std::endl ;
            out << "        event.stopPropagation();" << std::endl ;
            out << "" << std::endl ;
            out << "        var delta = 0;" << std::endl ;
            out << "" << std::endl ;
            out << "        if ( event.wheelDelta ) { // WebKit / Opera / Explorer 9"
                << std::endl ;
            out << "" << std::endl ;
            out << "            delta = event.wheelDelta / 40;" << std::endl ;
            out << "" << std::endl ;
            out << "        } else if ( event.detail ) { // Firefox" << std::endl ;
            out << "" << std::endl ;
            out << "            delta = - event.detail / 3;" << std::endl ;
            out << "" << std::endl ;
            out << "        }" << std::endl ;
            out << "" << std::endl ;
            out << "        _zoomStart.y += delta * 0.01;" << std::endl ;
            out << "        _this.dispatchEvent( startEvent );" << std::endl ;
            out << "        _this.dispatchEvent( endEvent );" << std::endl ;
            out << "" << std::endl ;
            out << "    }" << std::endl ;
            out << "" << std::endl ;
            out << "    function touchstart( event ) {" << std::endl ;
            out << "" << std::endl ;
            out << "        if ( _this.enabled === false ) return;" << std::endl ;
            out << "" << std::endl ;
            out << "        switch ( event.touches.length ) {" << std::endl ;
            out << "" << std::endl ;
            out << "            case 1:" << std::endl ;
            out << "                _state = STATE.TOUCH_ROTATE;" << std::endl ;
            out
                << "                _moveCurr.copy( getMouseOnCircle( event.touches[ 0 ].pageX, event.touches[ 0 ].pageY ) );"
                << std::endl ;
            out << "                _movePrev.copy(_moveCurr);" << std::endl ;
            out << "                break;" << std::endl ;
            out << "" << std::endl ;
            out << "            case 2:" << std::endl ;
            out << "                _state = STATE.TOUCH_ZOOM_PAN;" << std::endl ;
            out
                << "                var dx = event.touches[ 0 ].pageX - event.touches[ 1 ].pageX;"
                << std::endl ;
            out
                << "                var dy = event.touches[ 0 ].pageY - event.touches[ 1 ].pageY;"
                << std::endl ;
            out
                << "                _touchZoomDistanceEnd = _touchZoomDistanceStart = Math.sqrt( dx * dx + dy * dy );"
                << std::endl ;
            out << "" << std::endl ;
            out
                << "                var x = ( event.touches[ 0 ].pageX + event.touches[ 1 ].pageX ) / 2;"
                << std::endl ;
            out
                << "                var y = ( event.touches[ 0 ].pageY + event.touches[ 1 ].pageY ) / 2;"
                << std::endl ;
            out << "                _panStart.copy( getMouseOnScreen( x, y ) );"
                << std::endl ;
            out << "                _panEnd.copy( _panStart );" << std::endl ;
            out << "                break;" << std::endl ;
            out << "" << std::endl ;
            out << "            default:" << std::endl ;
            out << "                _state = STATE.NONE;" << std::endl ;
            out << "" << std::endl ;
            out << "        }" << std::endl ;
            out << "        _this.dispatchEvent( startEvent );" << std::endl ;
            out << "" << std::endl ;
            out << "" << std::endl ;
            out << "    }" << std::endl ;
            out << "" << std::endl ;
            out << "    function touchmove( event ) {" << std::endl ;
            out << "" << std::endl ;
            out << "        if ( _this.enabled === false ) return;" << std::endl ;
            out << "" << std::endl ;
            out << "        event.preventDefault();" << std::endl ;
            out << "        event.stopPropagation();" << std::endl ;
            out << "" << std::endl ;
            out << "        switch ( event.touches.length ) {" << std::endl ;
            out << "" << std::endl ;
            out << "            case 1:" << std::endl ;
            out << "                _movePrev.copy(_moveCurr);" << std::endl ;
            out
                << "                _moveCurr.copy( getMouseOnCircle(  event.touches[ 0 ].pageX, event.touches[ 0 ].pageY ) );"
                << std::endl ;
            out << "                break;" << std::endl ;
            out << "" << std::endl ;
            out << "            case 2:" << std::endl ;
            out
                << "                var dx = event.touches[ 0 ].pageX - event.touches[ 1 ].pageX;"
                << std::endl ;
            out
                << "                var dy = event.touches[ 0 ].pageY - event.touches[ 1 ].pageY;"
                << std::endl ;
            out
                << "                _touchZoomDistanceEnd = Math.sqrt( dx * dx + dy * dy );"
                << std::endl ;
            out << "" << std::endl ;
            out
                << "                var x = ( event.touches[ 0 ].pageX + event.touches[ 1 ].pageX ) / 2;"
                << std::endl ;
            out
                << "                var y = ( event.touches[ 0 ].pageY + event.touches[ 1 ].pageY ) / 2;"
                << std::endl ;
            out << "                _panEnd.copy( getMouseOnScreen( x, y ) );"
                << std::endl ;
            out << "                break;" << std::endl ;
            out << "" << std::endl ;
            out << "            default:" << std::endl ;
            out << "                _state = STATE.NONE;" << std::endl ;
            out << "" << std::endl ;
            out << "        }" << std::endl ;
            out << "" << std::endl ;
            out << "    }" << std::endl ;
            out << "" << std::endl ;
            out << "    function touchend( event ) {" << std::endl ;
            out << "" << std::endl ;
            out << "        if ( _this.enabled === false ) return;" << std::endl ;
            out << "" << std::endl ;
            out << "        switch ( event.touches.length ) {" << std::endl ;
            out << "" << std::endl ;
            out << "            case 1:" << std::endl ;
            out << "                _movePrev.copy(_moveCurr);" << std::endl ;
            out
                << "                _moveCurr.copy( getMouseOnCircle(  event.touches[ 0 ].pageX, event.touches[ 0 ].pageY ) );"
                << std::endl ;
            out << "                break;" << std::endl ;
            out << "" << std::endl ;
            out << "            case 2:" << std::endl ;
            out
                << "                _touchZoomDistanceStart = _touchZoomDistanceEnd = 0;"
                << std::endl ;
            out << "" << std::endl ;
            out
                << "                var x = ( event.touches[ 0 ].pageX + event.touches[ 1 ].pageX ) / 2;"
                << std::endl ;
            out
                << "                var y = ( event.touches[ 0 ].pageY + event.touches[ 1 ].pageY ) / 2;"
                << std::endl ;
            out << "                _panEnd.copy( getMouseOnScreen( x, y ) );"
                << std::endl ;
            out << "                _panStart.copy( _panEnd );" << std::endl ;
            out << "                break;" << std::endl ;
            out << "" << std::endl ;
            out << "        }" << std::endl ;
            out << "" << std::endl ;
            out << "        _state = STATE.NONE;" << std::endl ;
            out << "        _this.dispatchEvent( endEvent );" << std::endl ;
            out << "" << std::endl ;
            out << "    }" << std::endl ;
            out << "" << std::endl ;
            out
                << "    this.domElement.addEventListener( 'contextmenu', function ( event ) { event.preventDefault(); }, false );"
                << std::endl ;
            out << "" << std::endl ;
            out
                << "    this.domElement.addEventListener( 'mousedown', mousedown, false );"
                << std::endl ;
            out << "" << std::endl ;
            out
                << "    this.domElement.addEventListener( 'mousewheel', mousewheel, false );"
                << std::endl ;
            out
                << "    this.domElement.addEventListener( 'DOMMouseScroll', mousewheel, false ); // firefox"
                << std::endl ;
            out << "" << std::endl ;
            out
                << "    this.domElement.addEventListener( 'touchstart', touchstart, false );"
                << std::endl ;
            out
                << "    this.domElement.addEventListener( 'touchend', touchend, false );"
                << std::endl ;
            out
                << "    this.domElement.addEventListener( 'touchmove', touchmove, false );"
                << std::endl ;
            out << "" << std::endl ;
            out << "    window.addEventListener( 'keydown', keydown, false );"
                << std::endl ;
            out << "    window.addEventListener( 'keyup', keyup, false );"
                << std::endl ;
            out << "" << std::endl ;
            out << "    this.handleResize();" << std::endl ;
            out << "" << std::endl ;
            out << "    // force an update at start" << std::endl ;
            out << "    this.update();" << std::endl ;
            out << "" << std::endl ;
            out << "};" << std::endl ;
            out << "" << std::endl ;
            out
                << "THREE.TrackballControls.prototype = Object.create( THREE.EventDispatcher.prototype );"
                << std::endl ;
            out
                << "THREE.TrackballControls.prototype.constructor = THREE.TrackballControls;"
                << std::endl ;
        }

        virtual bool load( const std::string& filename, GeoModel& model )
        {
            GEO::Logger::err( "I/O" )
                << "Loading of a MacroMesh from WebGL mesh not implemented yet"
                << std::endl ;
            return false ;
        }

        virtual bool save( const GeoModel& model, const std::string& filename )
        {
            std::string sep = ", " ;
            std::ofstream out( filename.c_str() ) ;
            out.precision( 16 ) ;
            out << "<html>" << std::endl ;
            out << "<head>" << std::endl ;
            out << "<title>" << std::endl ;
            out << "Visualisation of structural model from RINGMesh" << std::endl ;
            out << "</title>" << std::endl ;
            out << "<style>" << std::endl ;
            out << "body {margin: 0;} canvas { width: 100%; height: 100% }"
                << std::endl ;
            out << "</style>" << std::endl ;
            out << "</head>" << std::endl ;
            out << "<body>" << std::endl ;

            out << "<script>" << std::endl ;
            print_three_min( out ) ;
            out << "</script>" << std::endl ;

            out << "<script>" << std::endl ;
            print_TrackballControls( out ) ;
            out << "</script>" << std::endl ;

            out << "<script>" << std::endl ;
            print_OrbitControls( out ) ;
            out << "</script>" << std::endl ;

            out << "<script>" << std::endl ;
            print_KeyboardState( out ) ;
            out << "</script>" << std::endl ;

            out << "<script>" << std::endl ;
            print_script01( out ) ;
            out << "</script>" << std::endl ;

            out << "" << std::endl ;
            out << "<script>" << std::endl ;
            out << "var scene,camera,material,light,ambientLight,renderer;"
                << std::endl ;
            out << "var meshes=[];" << std::endl ;

            for( index_t i = 0; i < model.nb_interfaces(); i++ ) {
                const GeoModelElement& interf = model.one_interface( i ) ;
                if( interf.is_on_voi() ) {
                    continue ;
                } else {
                    index_t nb_pts = 0 ;
                    index_t nb_trgl = 0 ;
                    for( index_t s = 0; s < interf.nb_children(); s++ ) {
                        const Surface& surface =
                            dynamic_cast< const Surface& >( interf.child( s ) ) ;
                        nb_pts += surface.nb_vertices() ;
                        nb_trgl += surface.nb_cells() ;
                    }
                    out << "var " << interf.name() << " = [ " << nb_pts << sep
                        << nb_trgl ;
                    for( index_t s = 0; s < interf.nb_children(); s++ ) {
                        const Surface& surface =
                            dynamic_cast< const Surface& >( interf.child( s ) ) ;
                        for( index_t p = 0; p < surface.nb_vertices(); p++ ) {
                            const vec3& point = surface.vertex( p ) ;
                            out << sep << point.x << sep << point.y << sep
                                << point.z ;
                        }
                    }
                    index_t offset = 0 ;
                    for( index_t s = 0; s < interf.nb_children(); s++ ) {
                        const Surface& surface =
                            dynamic_cast< const Surface& >( interf.child( s ) ) ;
                        for( index_t c = 0; c < surface.nb_cells(); c++ ) {
                            out << sep << offset + surface.surf_vertex_id( c, 0 )
                                << sep << offset + surface.surf_vertex_id( c, 1 )
                                << sep << offset + surface.surf_vertex_id( c, 2 ) ;
                        }
                        offset += surface.nb_vertices() ;
                    }
                    out << " ] ;" << std::endl ;
                }
            }

            out << "init();render();" << std::endl ;
            out << "function loadObjects(){" << std::endl ;
            out << "var i = 0;" << std::endl ;

            for( index_t i = 0; i < model.nb_interfaces(); i++ ) {
                const GeoModelElement& interf = model.one_interface( i ) ;
                if( interf.is_on_voi() ) {
                    continue ;
                } else {
                    out << "loadTSurf(" << interf.name() << ");" << std::endl ;
                    out << "meshes[i++].material.color = new THREE.Color(\"rgb("
                        << GEO::Numeric::random_int32() % 256 << ","
                        << GEO::Numeric::random_int32() % 256 << ","
                        << GEO::Numeric::random_int32() % 256 << ")\");"
                        << std::endl ;
                }
            }

            out << "addMeshes();" << std::endl ;
            out << "};" << std::endl ;
            out << "</script>" << std::endl ;
            out << "</body>" << std::endl ;
            out << "</html>" << std::endl ;

            return true ;
        }
    } ;

    class ParaviewIOHandler: public GeoModelSurfaceIOHandler {
    public:
        virtual bool load( const std::string& filename, GeoModel& model )
        {
            GEO::Logger::err( "I/O" )
                << "Loading of a MacroMesh from UCD mesh not implemented yet"
                << std::endl ;
            return false ;
        }

        virtual bool save( const GeoModel& model, const std::string& filename )
        {
            // Create a new directory to outputs the files
            std::string full_path ;
            std::string directory ;
            create_directory_from_filename( filename, full_path, directory ) ;

            // Each interface is saved as a *.vtp.
            // The main file (*.pvd) stores the collection of the others files (*.vtp).
            std::ostringstream pvd_oss ;
            pvd_oss << full_path << "/" << directory << ".pvd" ;
            std::ofstream pvd( pvd_oss.str().c_str() ) ;

            pvd << "<?xml version=\"1.0\"?>" << std::endl ;
            pvd << "<VTKFile type=\"Collection\" version=\"0.1\" "
                << "byte_order=\"LittleEndian\" >" << std::endl ;
            pvd << "<Collection>" << std::endl ;

            for( index_t i = 0; i < model.nb_interfaces(); i++ ) {
                const GeoModelElement& interf = model.one_interface( i ) ;
                const std::string& name = interf.name() ;

                // Create the current *.vtp file
                std::ostringstream cur_oss ;
                cur_oss << full_path << "/" << name << ".vtp" ;
                std::ofstream out( cur_oss.str().c_str() ) ;
                out.precision( 16 ) ;

                pvd << "<DataSet group=\"\" part=\"0\" file=\"" << name
                    << ".vtp\" />" << std::endl ;

                out << "<?xml version=\"1.0\"?>" << std::endl ;
                out
                    << "<VTKFile type=\"PolyData\" version=\"0.1\" byte_order=\"LittleEndian\">"
                    << std::endl ;
                out << "<PolyData>" << std::endl ;

                index_t nb_vertices = 0 ;
                index_t nb_polygons = 0 ;
                for( index_t s = 0; s < interf.nb_children(); s++ ) {
                    const Surface& surface =
                        dynamic_cast< const Surface& >( interf.child( s ) ) ;
                    nb_vertices += surface.nb_vertices() ;
                    nb_polygons += surface.nb_cells() ;
                }

                // Output all the vertices in one line
                out << "<Piece NumberOfPoints=\"" << nb_vertices
                    << "\" NumberOfVerts=\"0\" NumberOfLines=\"0\" NumberOfStrips=\"0\" NumberOfPolys=\""
                    << nb_polygons << "\">" << std::endl ;
                out << "<Points>" << std::endl ;
                out
                    << "<DataArray type=\"Float64\" NumberOfComponents=\"3\" format=\"ascii\">"
                    << std::endl ;
                for( index_t s = 0; s < interf.nb_children(); s++ ) {
                    const GeoModelMeshElement& surface =
                        dynamic_cast< const GeoModelMeshElement& >( interf.child( s ) ) ;
                    for( index_t v = 0; v < surface.nb_vertices(); v++ ) {
                        out << surface.vertex( v ) << " " ;
                    }
                }
                out << std::endl ;
                out << "</DataArray>" << std::endl ;
                out << "</Points>" << std::endl ;

                // Start output polygons
                out << "<Polys>" << std::endl ;

                // Output vertex indices in one line
                out
                    << "<DataArray type=\"Int32\" Name=\"connectivity\" format=\"ascii\">"
                    << std::endl ;
                index_t vertex_offset = 0 ;
                for( index_t s = 0; s < interf.nb_children(); s++ ) {
                    const Surface& surface =
                        dynamic_cast< const Surface& >( interf.child( s ) ) ;
                    for( index_t f = 0; f < surface.nb_cells(); f++ ) {
                        for( index_t v = 0; v < surface.nb_vertices_in_facet( f );
                            v++ ) {
                            out << surface.surf_vertex_id( f, v ) + vertex_offset
                                << " " ;
                        }
                    }
                    vertex_offset += surface.nb_vertices() ;
                }
                out << std::endl ;
                out << "</DataArray>" << std::endl ;

                // Output upper limit for iterating over the
                // vertex indices (equivalent to facet_end() )
                out << "<DataArray type=\"Int32\" Name=\"offsets\" format=\"ascii\">"
                    << std::endl ;
                index_t facet_offset = 0 ;
                for( index_t s = 0; s < interf.nb_children(); s++ ) {
                    const Surface& surface =
                        dynamic_cast< const Surface& >( interf.child( s ) ) ;
                    for( index_t f = 0; f < surface.nb_cells(); f++ ) {
                        facet_offset += surface.nb_vertices_in_facet( f ) ;
                        out << facet_offset << " " ;
                    }
                }
                out << std::endl ;

                out << "</DataArray>" << std::endl ;
                out << "</Polys>" << std::endl ;
                out << "</Piece>" << std::endl ;
                out << "</PolyData>" << std::endl ;
                out << "</VTKFile>" << std::endl ;
            }

            pvd << "</Collection>" << std::endl ;
            pvd << "</VTKFile>" << std::endl ;

            return true ;
        }
    } ;

    /************************************************************************/

    /*!
     * Loads a GeoModel from a file
     * @param[in] filename the file to load
     * @param[out] model the model to fill
     * @return returns the success of the operation
     */
    bool geomodel_surface_load( const std::string& filename, GeoModel& model )
    {
        if( filename.empty() ) {
            GEO::Logger::err( "I/O" )
                << "No filename provided for structural model, use in:model"
                << std::endl ;
            return false ;
        }
        std::ifstream input( filename.c_str() ) ;
        if( !input ) {
            GEO::Logger::err( "I/O" ) << "Cannot open file: " << filename
                << std::endl ;
            return false ;
        }

        GEO::Logger::out( "I/O" ) << "Loading file: " << filename
            << std::endl ;

        GeoModelSurfaceIOHandler_var handler = GeoModelSurfaceIOHandler::get_handler(
            filename ) ;
        if( handler == nil ) {
            return false ;
        }
        else {
            bool success = handler->load( filename, model ) ;
            if( !success ) {
                GEO::Logger::err( "I/O" ) << "Could not load file: " << filename
                    << std::endl ;
            }
            return success ;
        }
    }

    /*!
     * Saves a GeoModel in a file
     * @param[in] model the model to save
     * @param[in] filename the filename where to save it
     * @return returns the success of the operation
     */
    bool geomodel_surface_save( const GeoModel& model, const std::string& filename )
    {
        GEO::Logger::out( "I/O" ) << "Saving file " << filename
            << std::endl ;

        GeoModelSurfaceIOHandler_var handler = 
            GeoModelSurfaceIOHandler::get_handler( filename ) ;
        if( handler == nil ) {
            return false ;
        }
        else {
            bool success = handler->save( model, filename ) ;
            if( !success ) {
                GEO::Logger::err( "I/O" ) << "Could not save file: " << filename
                    << std::endl ;
            }
            return success ;
        }        
    }

    /************************************************************************/

    GeoModelSurfaceIOHandler* GeoModelSurfaceIOHandler::create(
        const std::string& format )
    {
        GeoModelSurfaceIOHandler* handler =
            GeoModelSurfaceIOHandlerFactory::create_object( format ) ;
        if( handler == nil ) {
            std::vector< std::string > names ;
            GeoModelSurfaceIOHandlerFactory::list_creators( names ) ;
            GEO::Logger::err( "I/O" ) << "Unsupported file format: " << format
                << "Currently supported file formats are: " ;
            for( index_t i = 0; i < names.size(); i++ ) {
                GEO::Logger::out( "I/O" ) << " " << names[ i ] ;
            }
            GEO::Logger::out( "I/O" ) << std::endl ;
        }
        return handler ;        
    }

    GeoModelSurfaceIOHandler* GeoModelSurfaceIOHandler::get_handler(
        const std::string& filename )
    {
        return create( GEO::FileSystem::extension( filename ) ) ;
    }

    /*
     * Initializes the possible handlers for IO GeoModel files
     */
    void GeoModelSurfaceIOHandler::initialize()
    {
        ringmesh_register_GeoModelSurfaceIOHandler_creator( MLIOHandler, "ml" );
        ringmesh_register_GeoModelSurfaceIOHandler_creator( BMIOHandler, "bm" ) ;
        ringmesh_register_GeoModelSurfaceIOHandler_creator( UCDIOHandler, "inp" ) ;
        ringmesh_register_GeoModelSurfaceIOHandler_creator( WebGLIOHandler, "html" ) ;
        ringmesh_register_GeoModelSurfaceIOHandler_creator( ParaviewIOHandler, "paraview" ) ;
    }
}
