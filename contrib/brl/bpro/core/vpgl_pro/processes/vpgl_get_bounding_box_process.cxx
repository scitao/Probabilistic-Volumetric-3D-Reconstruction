// This is brl/bpro/core/vpgl_pro/processes/vpgl_get_bounding_box_process.cxx
#include <bprb/bprb_func_process.h>
//:
// \file

#include <bprb/bprb_parameters.h>
#include <vcl_iostream.h>
#include <vcl_fstream.h>
#include <vgl/vgl_plane_3d.h>
#include <vgl/vgl_intersection.h>
#include <vpgl/algo/vpgl_camera_bounds.h>
#include <vgl/vgl_polygon_scan_iterator.h>
#include <vgl/vgl_polygon.h>
#include <vgl/vgl_box_2d.h>
#include <vsl/vsl_binary_io.h>
#include <vil/vil_image_view.h>
#include <vil/vil_save.h>
#include <boxm2/boxm2_util.h>

//: initialization
bool vpgl_get_bounding_box_process_cons(bprb_func_process& pro)
{
  //this process takes two inputs:
  //input[0]: the camera
  vcl_vector<vcl_string> input_types;
  input_types.push_back("vcl_string");    //directory of perspective cameras
  pro.set_input_types(input_types);

  vcl_vector<vcl_string> output_types;
  //output_types.push_back("float");
  pro.set_output_types(output_types);
  return true;
}

//: Execute the process
bool vpgl_get_bounding_box_process(bprb_func_process& pro)
{
  if (pro.n_inputs()<1) {
    vcl_cout << "vpgl_get_bounding_box_process: The input number should be 1 string" << vcl_endl;
    return false;
  }

  // get the inputs
  int i=0;
  vcl_string cam_dir = pro.get_input<vcl_string>(i);
  float      zplane  = 0.0; 

  //populate vector of cameras
  //: returns a list of cameras from specified directory
  vcl_vector<vpgl_perspective_camera<double>* > cams = 
      boxm2_util::cameras_from_directory(cam_dir); 
  
  //run planar bounding box
  vgl_box_2d<double> bbox; 
  bool good = vpgl_camera_bounds::planar_bouding_box(cams,bbox,zplane); 
  if(good) 
    vcl_cout<<"Bounding box found: "<<bbox<<vcl_endl;
  else 
    vcl_cout<<"Bounding box not found. "<<vcl_endl;
  
  
  //---------------------------------------------------------------------------
  //create zplane count map
  //---------------------------------------------------------------------------
  //determine the resolution of a pixel on the z plane
  vpgl_perspective_camera<double> cam = *cams[0]; 
  vgl_point_2d<double> pp = (cam.get_calibration()).principal_point();
  vgl_ray_3d<double> cone_axis;
  double cone_half_angle, solid_angle;
  vpgl_camera_bounds::pixel_solid_angle(cam, pp.x()/4, pp.y()/4, cone_axis, cone_half_angle, solid_angle);
  vgl_point_3d<double> cc = cam.camera_center();
  vgl_point_3d<double> zc( bbox.centroid().x(), bbox.centroid().y(), zplane); 
  double res = 2*(cc-zc).length()*cone_half_angle;
  
  //create an image with this res, and count each pixel
  unsigned ni = (unsigned) (bbox.width()/res); 
  unsigned nj = (unsigned) (bbox.height()/res);
  vil_image_view<vxl_byte> cntimg(ni, nj); 
  vcl_cout<<"Created Box size: "<<ni<<','<<nj<<vcl_endl;
  for(int i=0; i<cams.size(); ++i) {
    
    //project the four corners to the ground plane
    cam = *cams[i]; 
    vgl_ray_3d<double> ul = cam.backproject(0.0, 0.0);
    vgl_ray_3d<double> ur = cam.backproject(2*pp.x(), 0.0); 
    vgl_ray_3d<double> bl = cam.backproject(0.0, 2*pp.y()); 
    vgl_ray_3d<double> br = cam.backproject(2*pp.x(), 2*pp.y()); 

    //define z plane
    vgl_plane_3d<double> zp( vgl_point_3d<double>( 1.0,  1.0, zplane),
                             vgl_point_3d<double>( 1.0, -1.0, zplane),
                             vgl_point_3d<double>(-1.0,  1.0, zplane) );

    //intersect each ray with z plane
    vgl_point_3d<double> ulp, urp, blp, brp; 
    bool good =    vgl_intersection(ul, zp, ulp); 
    good = good && vgl_intersection(ur, zp, urp); 
    good = good && vgl_intersection(bl, zp, blp); 
    good = good && vgl_intersection(br, zp, brp); 
    
    //convert the four corners into image coordinates
    typedef vgl_polygon<double>::point_t        Point_type;
    typedef vgl_polygon<double>                 Polygon_type;
    typedef vgl_polygon_scan_iterator<double>   Polygon_scan; 
    Polygon_type poly;
    poly.new_sheet();    
    poly.push_back( Point_type( (ulp.x()-bbox.min_x())/res, (ulp.y()-bbox.min_y())/res ) ); 
    poly.push_back( Point_type( (urp.x()-bbox.min_x())/res, (urp.y()-bbox.min_y())/res ) ); 
    poly.push_back( Point_type( (blp.x()-bbox.min_x())/res, (blp.y()-bbox.min_y())/res ) ); 
    poly.push_back( Point_type( (brp.x()-bbox.min_x())/res, (brp.y()-bbox.min_y())/res ) ); 

    // There will be scan lines at y=0, 1 and 2.
    unsigned int count=0;
    Polygon_scan it(poly, false);
    int y=0;
    for (it.reset(); it.next(); ++y) {
      int y = it.scany();
      for (int x = it.startx(); x <= it.endx(); ++x) {
        int yy = nj-y; 
        if(x>=0 && x<ni && yy>=0 && yy<nj) {
          cntimg(x, nj-y) += (vxl_byte) 1; 
        }
        else{
          //vcl_cout<<"X and Y in scan iterator are out of bounds: "<<x<<','<<y<<vcl_endl;
        }
      }
    }
  }
  
  //use count image to create a tighter bounding box
  vil_save(cntimg, "countImage.png");

  
  //clean up cameras
  for(int i=0; i<cams.size(); ++i) 
    delete cams[i]; 
  
  return good;
}
