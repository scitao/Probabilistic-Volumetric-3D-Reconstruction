#ifndef bwm_observer_cam_h_
#define bwm_observer_cam_h_
//:
// \file

#include "bwm_observer_vgui.h"

#include <vcl_iostream.h>

#include <vgui/vgui_easy2d_tableau.h>
#include <vgui/vgui_viewer2D_tableau.h>

#include <vgl/vgl_point_2d.h>
#include <vgl/vgl_point_3d.h>
#include <vgl/vgl_homg_plane_3d.h>
#include <vgl/vgl_homg_point_2d.h>

#include <vsol/vsol_point_2d_sptr.h>
#include <vsol/vsol_point_3d_sptr.h>
#include <vsol/vsol_polygon_2d_sptr.h>
#include <vsol/vsol_polygon_3d_sptr.h>

#include <vpgl/vpgl_camera.h>

class bwm_observer_cam : public bwm_observer_vgui
{
 public:

  typedef bwm_observer_vgui base;

  bwm_observer_cam(bgui_image_tableau_sptr const& img, vpgl_camera<double> *camera, vcl_string cam_path)
    : bwm_observer_vgui(img), proj_plane_(vgl_homg_plane_3d<double>(0, 0, 1, 0)),
    camera_(camera), img_tab_(img), viewer_(0), cam_path_(cam_path), cam_adjusted_(false)
  {  }

  // set the initial projection plane to z=0
  bwm_observer_cam(bgui_image_tableau_sptr const& img, const char* n="unnamed")
    : bwm_observer_vgui(img), proj_plane_(vgl_homg_plane_3d<double>(0, 0, 1, 0)),
    viewer_(0) { }

  virtual ~bwm_observer_cam(){ delete camera_;}

  bgui_image_tableau_sptr image_tableau() { return img_tab_; }

  vcl_string camera_path() { return cam_path_; }

#if 0
  //tableau_img has the viewer - JLM
    void set_viewer(vgui_viewer2D_tableau_sptr viewer) { viewer_ = viewer; }
#endif
  bool handle(const vgui_event &e);

  virtual vcl_string type_name() const { return "bwm_observer_cam"; }

  void set_camera(vpgl_camera<double> *camera, vcl_string cam_path)
  { camera_ = camera; cam_path_ = cam_path; cam_adjusted_ = false;}

  vpgl_camera<double> * camera(){return camera_;}

  bool camera_adjusted() { return cam_adjusted_; }

  void set_proj_plane(vgl_homg_plane_3d<double> proj_plane)
  { proj_plane_ = proj_plane; }

  void select_proj_plane();

  void move_ground_plane(vgl_homg_plane_3d<double> master_plane,
                         vsol_point_2d_sptr new_pt);

  vgl_homg_plane_3d<double> get_proj_plane() { return proj_plane_; }

  void set_ground_plane(double x1, double y1, double x2, double y2);

  //: if true, makes all the objects selectable, otherwise unselectable
  void set_selection(bool status);

  void backproj_point(vsol_point_2d_sptr p2d, vsol_point_3d_sptr& p3d);

  void backproj_point(vsol_point_2d_sptr p2d,
    vsol_point_3d_sptr& p3d,
    vgl_homg_plane_3d<double> proj_plane);

  //: Special case of backprojecting onto the projection plane
  void backproj_poly(vsol_polygon_2d_sptr poly2d,
    vsol_polygon_3d_sptr& poly3d) {
      backproj_poly(poly2d, poly3d, proj_plane_);
  }

  //: Special case of backprojecting onto the projection plane translated by dist in the direction of the normal
  void backproj_poly(vsol_polygon_2d_sptr poly2d,
                              vsol_polygon_3d_sptr& poly3d,
                              double dist);

  void proj_point(vgl_point_3d<double> world_pt,
                  vgl_point_2d<double> &image_pt);

  void proj_poly(vsol_polygon_3d_sptr poly3d,
                 vsol_polygon_2d_sptr& poly2d);

#if 0
  void proj_poly(vcl_vector<bmsh3d_vertex*> verts,
                 vcl_vector<vgl_point_2d<double> > &projections);
#endif

  bool intersect(float x1, float y1, float x2, float y2);

  bool intersect(bwm_observable_sptr obj, unsigned face_id,
                 float x1, float y1, float x2, float y2);

  bool intersect(bwm_observable_sptr obj, float img_x, float img_y,
                 unsigned face_id, vgl_point_3d<double> &pt3d);

  bool find_intersection_point(vgl_homg_point_2d<double> img_point,
                               vsol_polygon_3d_sptr poly3d,
                               vgl_point_3d<double>& point3d);

  virtual void camera_center(vgl_homg_point_3d<double> &center){};

  bool corr_pt(vgl_point_2d<double> &p)
  { if (corr_.second) {p = corr_.first; return true;} else return false; }

  //virtual vgl_vector_3d<double> camera_direction(vgl_point_3d<double> origin)=0;

  void extrude_face(vsol_point_2d_sptr pt);

  void divide_face(bwm_observable_sptr obs, unsigned face_id,
    float x1, float y1, float x2, float y2);

  void scan_regions();

  void load_mesh();

  void load_mesh_multiple();

  void save();
  void save(vcl_string path);
  void save_all();
  void save_all(vcl_string path);

  void triangulate_meshes();

  void move_corr_point(vsol_point_2d_sptr new_pt);

  void set_corr_to_vertex();

  void world_pt_corr();

  virtual vcl_ostream& print_camera(vcl_ostream& s) {return s;}

 protected:

  vpgl_camera<double> *camera_;

  vcl_string cam_path_;

  bool cam_adjusted_;

  vgl_homg_plane_3d<double> proj_plane_;

  bgui_image_tableau_sptr img_tab_;

  vgui_viewer2D_tableau_sptr viewer_;

  bwm_observer_cam() {}

  virtual bool intersect_ray_and_plane(vgl_homg_point_2d<double> img_point,
                                       vgl_homg_plane_3d<double> plane,
                                       vgl_homg_point_3d<double> &world_point)
  {vcl_cout << "ERROR!  USING CAM OBSERVER's intersect_ray_and_plane"; return false; }

  bool intersect_ray_and_box(vgl_box_3d<double> box,
                             vgl_homg_point_2d<double> img_point,
                             vgl_point_3d <double> &point);

  void backproj_poly(vsol_polygon_2d_sptr poly2d,
                     vsol_polygon_3d_sptr& poly3d,
                     vgl_homg_plane_3d<double> proj_plane);

  bool find_intersection_points(vgl_homg_point_2d<double> const img_point1,
                                vgl_homg_point_2d<double> const img_point2,
                                vsol_polygon_3d_sptr poly3d,
                                vgl_point_3d<double>& point1,
                                vgl_point_3d<double>& l1,
                                vgl_point_3d<double>& l2,
                                vgl_point_3d<double>& point2,
                                vgl_point_3d<double>& l3,
                                vgl_point_3d<double>& l4);

  unsigned find_index_of_v(bwm_soview2D_vertex* vertex, bgui_vsol_soview2D_polygon* polygon);

  void make_object_selectable(bwm_observable_sptr obj, bool status);
};

#endif
