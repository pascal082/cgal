// poisson_reconstruction_test.cpp


// ----------------------------------------------------------------------------
// USAGE EXAMPLES
// ----------------------------------------------------------------------------

//----------------------------------------------------------
// Test the Poisson Delaunay Reconstruction method
// No output
// Input files are .off and .xyz
//----------------------------------------------------------
// poisson_reconstruction_test mesh1.off point_set2.xyz...


#include <CGAL/basic.h> // include basic.h before testing #defines

#ifdef CGAL_USE_TAUCS


// CGAL
#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Timer.h>
#include <CGAL/IO/Polyhedron_iostream.h>

// Surface mesher
#include <CGAL/Surface_mesh_default_triangulation_3.h>
#include <CGAL/make_surface_mesh.h>
#include <CGAL/Implicit_surface_3.h>

// This package
#include <CGAL/Poisson_implicit_function.h>
#include <CGAL/Point_with_normal_3.h>
#include <CGAL/IO/surface_reconstruction_output.h>
#include <CGAL/IO/surface_reconstruction_read_xyz.h>

// This test
#include "enriched_polyhedron.h"

// STL stuff
#include <iostream>
#include <cstdlib>
#include <fstream>
#include <cassert>


// ----------------------------------------------------------------------------
// Private types
// ----------------------------------------------------------------------------

// kernel
typedef CGAL::Exact_predicates_inexact_constructions_kernel Kernel;
typedef Kernel::FT FT;
typedef Kernel::Point_3 Point;
typedef Kernel::Vector_3 Vector;
typedef CGAL::Point_with_normal_3<Kernel> Point_with_normal;
typedef Kernel::Sphere_3 Sphere;

// Poisson's Delaunay triangulation 3 and implicit function
typedef CGAL::Implicit_fct_delaunay_triangulation_3<Kernel> Dt3;
typedef CGAL::Poisson_implicit_function<Kernel, Dt3> Poisson_implicit_function;

// Surface mesher
typedef CGAL::Surface_mesh_default_triangulation_3 STr;
typedef CGAL::Surface_mesh_complex_2_in_triangulation_3<STr> C2t3;
typedef CGAL::Implicit_surface_3<Kernel, Poisson_implicit_function&> Surface_3;


// ----------------------------------------------------------------------------
// main()
// ----------------------------------------------------------------------------

int main(int argc, char * argv[])
{
  std::cerr << "RECONSTRUCTION" << std::endl;
  std::cerr << "Test the Poisson Delaunay Reconstruction method" << std::endl;
  std::cerr << "No output" << std::endl;

  //***************************************
  // decode parameters
  //***************************************

  if (argc-1 == 0)
  {
    std::cerr << "Usage: " << argv[0] << " mesh1.off point_set2.xyz ..." << std::endl;
    return(EXIT_FAILURE);
  }

  // Accumulated errors
  int accumulated_fatal_err = EXIT_SUCCESS;

  // Process each input file
  for (int arg_index = 1; arg_index <= argc-1; arg_index++)
  {
    std::cerr << std::endl;

    // File name is:
    std::string input_filename  = argv[arg_index];

    // get extension
    std::string extension = input_filename.substr(input_filename.find_last_of('.'));

    //***************************************
    // Load mesh/point set
    //***************************************

    Dt3 dt;

    if (extension == ".off" || extension == ".OFF")
    {
      // Read the mesh file in a polyhedron
      std::ifstream stream(input_filename.c_str());
      typedef Enriched_polyhedron<Kernel,Enriched_items> Polyhedron;
      Polyhedron input_mesh;
      CGAL::scan_OFF(stream, input_mesh, true /* verbose */);
      if(!stream || !input_mesh.is_valid() || input_mesh.empty())
      {
        std::cerr << "FATAL ERROR: cannot read file " << input_filename << std::endl;
        accumulated_fatal_err = EXIT_FAILURE;
        continue;
      }

      // Compute vertices' normals from connectivity
      input_mesh.compute_normals();

      // Insert vertices and normals in triangulation
      std::vector<Point_with_normal> pwns;
      Polyhedron::Vertex_iterator v;
      for(v = input_mesh.vertices_begin();
          v != input_mesh.vertices_end();
          v++)
      {
        const Point& p = v->point();
        const Vector& n = v->normal();
        pwns.push_back(Point_with_normal(p,n));
      }
      dt.insert(pwns.begin(), pwns.end());
    }
    else if (extension == ".xyz" || extension == ".XYZ")
    {
      // Read the point set file in a container of Point_with_normal
      std::list<Point_with_normal> pwns;
      if(!CGAL::surface_reconstruction_read_xyz(input_filename.c_str(),
                                                std::back_inserter(pwns)))
      {
        std::cerr << "FATAL ERROR: cannot read file " << input_filename << std::endl;
        accumulated_fatal_err = EXIT_FAILURE;
        continue;
      }

      // Insert vertices and normals in triangulation
      dt.insert(pwns.begin(), pwns.end());
    }
    else
    {
      std::cerr << "FATAL ERROR: cannot read file " << input_filename << std::endl;
      accumulated_fatal_err = EXIT_FAILURE;
      continue;
    }

    // Print status
    int nb_vertices = dt.number_of_vertices();
    std::cerr << "Read file " << input_filename << ": "
              << nb_vertices << " vertices"
              << std::endl;

    //***************************************
    // Compute implicit function
    //***************************************

    CGAL::Timer task_timer;
    task_timer.start();

    Poisson_implicit_function poisson_function(dt);

    /// Compute the Poisson indicator function f()
    /// at each vertex of the triangulation
    if ( ! poisson_function.compute_implicit_function() )
    {
      std::cerr << "FATAL ERROR: cannot solve Poisson equation" << std::endl;
      accumulated_fatal_err = EXIT_FAILURE;
      continue;
    }

    // Print status
    int nb_vertices2 = dt.number_of_vertices();
    std::cerr << "Solve Poisson equation: "
              << "(added " << nb_vertices2-nb_vertices << " vertices)"
              << std::endl;

    //***************************************
    // Surface mesh generation
    //***************************************

    // Surface mesher options
    FT sm_angle = 20.0; // theorical guaranty if angle >= 30, but slower
    FT sm_radius = 0.1; // as suggested by LR
    FT sm_distance = 0.005;
    FT sm_error_bound = 1e-3;

    STr tr;           // 3D-Delaunay triangulation
    C2t3 c2t3 (tr);   // 2D-complex in 3D-Delaunay triangulation

    // Get inner point
    Point inner_point = poisson_function.get_inner_point();
    FT inner_point_value = poisson_function(inner_point);
    if(inner_point_value >= 0.0)
    {
      std::cerr << "FATAL ERROR: unable to seed (" << inner_point_value << " at inner_point)" << std::endl;
      accumulated_fatal_err = EXIT_FAILURE;
      continue;
    }

    // Get implicit surface's size
    Sphere bounding_sphere = poisson_function.bounding_sphere();
    FT size = sqrt(bounding_sphere.squared_radius());

    // defining the surface
    Point sm_sphere_center = inner_point; // bounding sphere centered at inner_point
    FT    sm_sphere_radius = 2 * size;
    sm_sphere_radius *= 1.1; // <= the Surface Mesher fails if the sphere does not contain the surface
    Surface_3 surface(poisson_function,
                      Sphere(sm_sphere_center,sm_sphere_radius*sm_sphere_radius),
                      sm_error_bound*size/sm_sphere_radius); // dichotomy stops when segment < sm_error_bound*size

    // defining meshing criteria
    CGAL::Surface_mesh_default_criteria_3<STr> criteria(sm_angle,  // lower bound of facets angles (degrees)
                                                        sm_radius*size,  // upper bound of Delaunay balls radii
                                                        sm_distance*size); // upper bound of distance to surface

std::cerr << "Implicit_surface_3(dichotomy error="<<sm_error_bound*size << ")\n";
std::cerr << "make_surface_mesh(sphere={center=("<<sm_sphere_center << "), radius="<<sm_sphere_radius << "},\n"
          << "                  criteria={angle="<<sm_angle << ", radius="<<sm_radius*size << ", distance="<<sm_distance*size << "},\n"
          << "                  Non_manifold_tag())...\n";

    // meshing surface
    CGAL::make_surface_mesh(c2t3, surface, criteria, CGAL::Non_manifold_tag());

    // Print status
    std::cerr << "Surface meshing: " << task_timer.time() << " seconds, "
                                     << tr.number_of_vertices() << " vertices"
                                     << std::endl;
    task_timer.reset();

  } // for each input file

  std::cerr << std::endl;

  // Return accumulated fatal error
  std::cerr << "Tool returned " << accumulated_fatal_err << std::endl;
  return accumulated_fatal_err;
}


#else // CGAL_USE_TAUCS


#include <iostream>
#include <cstdlib>

// ----------------------------------------------------------------------------
// Empty main() if TAUCS is not installed
// ----------------------------------------------------------------------------

int main()
{
  std::cerr << "Skip test as TAUCS is not installed" << std::endl;
  return EXIT_SUCCESS;
}

#endif // CGAL_USE_TAUCS

