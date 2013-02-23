//:
// \file
// \brief executable to use object/ray based matcher for query image using obj_min_dist, obj_order, ground_depth, sky_region and orientation (if required)
// \author Yi Dong
// \date Feb 05, 2013


#include <volm/volm_io.h>
#include <volm/volm_tile.h>
#include <vul/vul_file.h>
#include <vul/vul_arg.h>
#include <volm/volm_geo_index.h>
#include <volm/volm_geo_index_sptr.h>
#include <volm/volm_spherical_container.h>
#include <volm/volm_spherical_container_sptr.h>
#include <volm/volm_spherical_shell_container.h>
#include <volm/volm_spherical_shell_container_sptr.h>
#include <volm/volm_query.h>
#include <volm/volm_query_sptr.h>
#include <volm/volm_camera_space.h>
#include <volm/volm_camera_space_sptr.h>
#include <volm/volm_loc_hyp.h>
#include <boxm2/volm/boxm2_volm_wr3db_index.h>
#include <boxm2/volm/boxm2_volm_wr3db_index_sptr.h>
#include <boxm2/volm/boxm2_volm_matcher_p1.h>
#include <bbas/bocl/bocl_manager.h>
#include <bbas/bocl/bocl_device.h>

int main(int argc, char** argv)
{
  // input
  vul_arg<vcl_string> cam_bin("-cam", "camera space binary", "");                                // query -- camera space binary
  vul_arg<vcl_string> dms_bin("-dms", "depth_map_scene binary", "");                             // query -- depth map scene
  vul_arg<vcl_string> sph_bin("-sph", "spherical shell binary", "");                             // query -- spherical shell container binary
  vul_arg<vcl_string> weight_file("-wgt", "weight parameters for query", "");                    // query -- weight parameter file
  vul_arg<vcl_string> geo_index_folder("-geo", "folder to read the geo index and the hypo", ""); // index -- folder to read the geo_index and hypos for each leaf
  vul_arg<vcl_string> candidate_list("-cand", "candidate list for given query (txt file)", "");  // index -- candidate list file containing polygons
  vul_arg<float>      buffer_capacity("-buff", "index buffer capacity (GB)", 1.0f);              // index -- buffer capacity
  vul_arg<unsigned>   tile_id("-tile", "ID of the tile that current matcher consdier", 3);       // matcher -- tile id
  vul_arg<unsigned>   dev_id("-gpuid", "device used for current matcher", 0);                    // matcher -- device id
  vul_arg<float>      threshold("-thres", "threshold for choosing valid cameras (0~1)", 0.4f);   // matcher -- threshold for choosing cameras
  vul_arg<unsigned>   max_cam_per_loc("-max_cam", "maximum number of cameras to be saved", 200); // matcher -- output related
  vul_arg<bool>       use_orient("-ori", "choose to use orientation attribute", false);          // matcher -- matcher option
  vul_arg<vcl_string> out_folder("-out", "output folder where score binary is stored", "");      // matcher -- output folder
  vul_arg<bool>       logger("-logger", "designate one of the exes as logger", false);           // matcher -- log file generation
#if 0
  vul_arg<bool>        use_ps0("-ps0", "choose to use pass 0 regional matcher", false);
  vul_arg<bool>        use_ps1("-ps1", "choose to use pass 1 obj_based order matcher", false);
  vul_arg<bool>        use_ps2("-ps2", "choose to use pass 2 obj_based orient matcher", false);
  vul_arg<bool>        gt_out("-gt", "choose to output camera score for ground truth locations", false);  // for testing purpose
  vul_arg<unsigned>    gt_l_id("-gtl", "leaf id for the ground truth location",0);                         // for testing purpose
  vul_arg<unsigned>    gt_h_id("-gth", "hypo id for the ground truth location",0);                         // for testing purpose
#endif

  vul_arg_parse(argc, argv);
  bool is_last_pass = false; // no previous matcher output

  vcl_stringstream log;
  bool do_log = false;
  if (logger())
    do_log = true;
  // check the input parameters
  if ( cam_bin().compare("") == 0 ||
       dms_bin().compare("") == 0 ||
       sph_bin().compare("") == 0 ||
       geo_index_folder().compare("") == 0 ||
       out_folder().compare("") == 0 )
  {
    log << " ERROR: input file/folders can not be empty\n";
    vcl_cerr << log.str();
    vul_arg_display_usage_and_exit();
    return volm_io::EXE_ARGUMENT_ERROR;
  }

  // load geo_index
  vcl_stringstream file_name_pre;
  file_name_pre << geo_index_folder() << "geo_index_tile_" << tile_id();
  vcl_cout << " geo_index_hyps_file = " << file_name_pre.str() + ".txt" << vcl_endl;
  if (!vul_file::exists(file_name_pre.str() + ".txt")) {
    log << " ERROR: gen_index_folder is wrong (missing last slash/ ?), no geo_index_files found in " << geo_index_folder() << vcl_endl;
    if (do_log) { volm_io::write_log(out_folder(), log.str()); }
    vcl_cerr << log.str() << vcl_endl;
    volm_io::write_status(out_folder(), volm_io::GEO_INDEX_FILE_MISSING);
    return volm_io::EXE_ARGUMENT_ERROR;
  }
  float min_size;
  volm_geo_index_node_sptr root = volm_geo_index::read_and_construct(file_name_pre.str() + ".txt", min_size);
  volm_geo_index::read_hyps(root, file_name_pre.str());
  // check whether we have candidate list for this query
  bool is_candidate = false;
  vgl_polygon<double> cand_poly;
  vcl_cout << " candidate list = " <<  candidate_list() << vcl_endl;
  if ( candidate_list().compare("") != 0) {
    if ( vul_file::extension(candidate_list()).compare(".txt") == 0) {
      is_candidate = true;
      volm_io::read_polygons(candidate_list(), cand_poly);
    }
    else {
      log << " ERROR: candidate list exist but with wrong format, only txt allowed" << candidate_list() << '\n';
      if (do_log)  volm_io::write_composer_log(out_folder(), log.str());
      vcl_cerr << log;
      volm_io::write_status(out_folder(), volm_io::EXE_ARGUMENT_ERROR);
      return volm_io::EXE_ARGUMENT_ERROR;
    }
  }
  else {
    vcl_cout << " NO candidate list for this query image, full index space is considered " << vcl_endl;
    is_candidate = false;
  }
  // prune the tree, only leaves with non-zero hypos are left
  if (is_candidate)
    volm_geo_index::prune_tree(root, cand_poly);
  vcl_vector<volm_geo_index_node_sptr> leaves;
  volm_geo_index::get_leaves_with_hyps(root, leaves);

  // read in the parameter, create depth_interval
  boxm2_volm_wr3db_index_params params;
  vcl_string index_file = leaves[0]->get_index_name(file_name_pre.str());

  if (!params.read_params_file(index_file)) {
    log << " ERROR: cannot read params file from " << index_file << '\n';
    if (do_log)  volm_io::write_log(out_folder(), log.str());
    volm_io::write_status(out_folder(), volm_io::EXE_ARGUMENT_ERROR);
    vcl_cerr << log.str() << vcl_endl;
    return volm_io::EXE_ARGUMENT_ERROR;
  }
  volm_spherical_container_sptr sph = new volm_spherical_container(params.solid_angle, params.vmin, params.dmax);
  // construct depth_interval table for pass 1 matcher
  vcl_map<double, unsigned char>& depth_interval_map = sph->get_depth_interval_map();
  vcl_vector<float> depth_interval;
  vcl_map<double, unsigned char>::iterator iter = depth_interval_map.begin();
  for (; iter != depth_interval_map.end(); ++iter)
    depth_interval.push_back((float)iter->first);

  // load spherical shell
  if ( !vul_file::exists(sph_bin()) ) {
    log << " ERROR: can not find spherical shell binary: " << sph_bin() << '\n';
    if (do_log) volm_io::write_log(out_folder(), log.str());
    volm_io::write_status(out_folder(), volm_io::EXE_ARGUMENT_ERROR);
    vcl_cerr << log.str() << vcl_endl;
    return volm_io::EXE_ARGUMENT_ERROR;
  }
  volm_spherical_shell_container_sptr sph_shell = new volm_spherical_shell_container();
  vsl_b_ifstream is_sph(sph_bin());
  sph_shell->b_read(is_sph);
  is_sph.close();
  if (sph_shell->get_container_size() != params.layer_size) {
    log << " ERROR: The loaded spherical shell has different layer size from the index\n";
    if (do_log) volm_io::write_log(out_folder(), log.str());
    volm_io::write_status(out_folder(), volm_io::EXE_ARGUMENT_ERROR);
    vcl_cerr << log.str() << vcl_endl;
    return volm_io::EXE_ARGUMENT_ERROR;
  }
  unsigned layer_size = (unsigned)sph_shell->get_container_size();

  // load camera space
  if (!vul_file::exists(cam_bin())) {
    vcl_cerr << " ERROR: camera_space binary --> " << cam_bin() << " can not be found!\n";
    volm_io::write_status(out_folder(), volm_io::EXE_ARGUMENT_ERROR);
    return volm_io::EXE_ARGUMENT_ERROR;
  }
  vsl_b_ifstream ifs_cam(cam_bin());
  volm_camera_space_sptr cam_space = new volm_camera_space();
  cam_space->b_read(ifs_cam);
  ifs_cam.close();

  // check depth_map_scene binary
  if (!vul_file::exists(dms_bin())) {
    vcl_cerr << " ERROR: depth map scene binary can not be found ---> " << dms_bin() << vcl_endl;
    volm_io::write_status(out_folder(), volm_io::DEPTH_SCENE_FILE_IO_ERROR);
    return volm_io::EXE_ARGUMENT_ERROR;
  }

  // create volm_query
  volm_query_sptr query = new volm_query(cam_space, dms_bin(), sph_shell, sph);


  // screen output of query
  unsigned total_size = query->obj_based_query_size_byte();
  vcl_cout << "\n==================================================================================================\n"
           << "\t\t  2. Create query from given camera space and Depth map scene\n"
           << "\t\t  " << dms_bin() << '\n'
           << "\t\t  generate query has " << query->get_cam_num() << " cameras "
           << " and " << (float)total_size/1024 << " Kbyte in total\n"
           << "==================================================================================================\n" << vcl_endl;
  vcl_cout << " The spherical shell for current query has parameters: point_angle = " << query->sph_shell()->point_angle()
           << ", top_angle = "    << query->sph_shell()->top_angle()
           << ", bottom_angle = " << query->sph_shell()->bottom_angle()
           << ", size = " << query->get_query_size() << vcl_endl;
  
  vcl_cout << " The depth interval used for current query has size " << depth_interval.size() 
           << ", max depth = " << depth_interval[depth_interval.size()-1] << vcl_endl;

  depth_map_scene_sptr dm = query->depth_scene();
  vcl_cout << " The " << dm->ni() << " x " << dm->nj() << " query image has following defined depth region" << vcl_endl;
  if (dm->sky().size()) {
    vcl_cout << " -------------- SKYs --------------" << vcl_endl;
    float sky_weight = query->sky_weight();
    for (unsigned i = 0; i < dm->sky().size(); i++)
      vcl_cout << "\t name = " << (dm->sky()[i]->name())
               << ",\t depth = " << 254
               << ",\t orient = " << (int)query->sky_orient()
               << ",\t weight = " << sky_weight
               << vcl_endl;
  }
  if (dm->ground_plane().size()) {
    vcl_cout << " -------------- GROUND PLANE --------------" << vcl_endl;
    for (unsigned i = 0; i < dm->ground_plane().size(); i++)
      vcl_cout << "\t name = " << dm->ground_plane()[i]->name()
               << ",\t depth = " << dm->ground_plane()[i]->min_depth()
               << ",\t orient = " << dm->ground_plane()[i]->orient_type()
               << ",\t land_id = " << dm->ground_plane()[i]->land_id()
               << ",\t land_name = " << volm_label_table::land_string(dm->ground_plane()[i]->land_id())
               << ",\t weight = " << query->grd_weight()
               << vcl_endl;
  }
  vcl_vector<depth_map_region_sptr> dmr = query->depth_regions();
  if (dmr.size()) {
    vcl_cout << " -------------- DEPTH REGIONS --------------" << vcl_endl;
    vcl_vector<float>& obj_weight = query->obj_weight();
    for (unsigned i = 0; i < dmr.size(); i++) {
      vcl_cout << "\t\t " <<  dmr[i]->name()  << " region "
               << ",\t\t min_depth = " << dmr[i]->min_depth()
               << " ---> interval = " << (int)sph->get_depth_interval(dmr[i]->min_depth())
               << ",\t\t max_depth = " << dmr[i]->max_depth()
               << ",\t\t order = " << dmr[i]->order()
               << ",\t\t orient = " << dmr[i]->orient_type()
               << ",\t\t NLCD_id = " << dmr[i]->land_id()
               << ",\t\t land_name = " << volm_label_table::land_string(dmr[i]->land_id())
               << " weight = " << obj_weight[i]
               << vcl_endl;
    }
  }

  // read (or create) weight parameters for depth_map_scene
  vcl_vector<volm_weight> weights;
  if (vul_file::exists(weight_file()) ) {
    // read the weight parameter from pre-loaded 
    volm_weight::read_weight(weights, weight_file());
  } else {
    // create equal weight parameter for all objects
    volm_weight::equal_weight(weights, dm);
  }

  // define the device that will be used
  bocl_manager_child_sptr mgr = bocl_manager_child::instance();
  if (dev_id() >= (unsigned)mgr->numGPUs()) {
    log << " GPU is " << dev_id() << " is invalid, only " << mgr->numGPUs() << " are available\n";
    if (do_log) volm_io::write_status(out_folder(), volm_io::EXE_ARGUMENT_ERROR);
    volm_io::write_log(out_folder(), log.str());
    vcl_cerr << log.str();
    return volm_io::EXE_ARGUMENT_ERROR;
  }
  vcl_cout << "\n==================================================================================================\n"
           << "\t\t  3. Following device is used for volm_matcher\n"
           << "\t\t  " << mgr->gpus_[dev_id()]->info() << '\n'
           << "==================================================================================================\n\n"

           << "\n==================================================================================================\n"
           << "\t\t  4. Start volumetric matching with following matchers\n"
           << "==================================================================================================\n" << vcl_endl;

  // start pass 1 volm_matcher
  boxm2_volm_matcher_p1 obj_ps1_matcher(query, leaves, buffer_capacity(), geo_index_folder(), tile_id(),
                                          depth_interval, cand_poly, mgr->gpus_[dev_id()], is_candidate, is_last_pass, out_folder(),
                                          threshold(), max_cam_per_loc(), use_orient());

  if (!obj_ps1_matcher.volm_matcher_p1()) {
    log << " ERROR: pass 1 volm_matcher failed for geo_index " << index_file << '\n';
    if (do_log) volm_io::write_log(out_folder(), log.str());
    volm_io::write_status(out_folder(), volm_io::MATCHER_EXE_FAILED);
    vcl_cerr << log.str() << vcl_endl;
    return volm_io::MATCHER_EXE_FAILED;
  }

  // write the score output binary
  vcl_cout << "\n==================================================================================================\n"
             << "\t\t  5. Generate output for pass 1 matcher and store it in\n"
             << "\t\t     " << out_folder() << '\n'
             << "==================================================================================================\n" << vcl_endl;
  vcl_stringstream out_fname_bin;
  out_fname_bin << out_folder() << "ps_1_scores_tile_" << tile_id() << ".bin";
#if 0
  vcl_stringstream out_fname_txt;
  out_fname_txt << out_folder() << "ps_1_scores_tile_" << tile_id() << ".txt";
#endif
  if (!obj_ps1_matcher.write_matcher_result(out_fname_bin.str())) {
    log << " ERROR: writing output failed for pass 1 ray_based matcher\n";
    if (do_log) volm_io::write_log(out_folder(), log.str());
    volm_io::write_status(out_folder(), volm_io::MATCHER_EXE_FAILED);
    vcl_cerr << log.str() << vcl_endl;
    return volm_io::MATCHER_EXE_FAILED;
  }

#if 0
  if (use_ps0()) {
    vcl_cout << " we will use pass 0, i.e. regional matcher... TO be implemented" << vcl_endl;
    is_last_pass = true;
  }
  else {
    vcl_cout << " regional matcher (pass 0) is avoided" << vcl_endl;
  }

  // start pass 1 matcher
  if (use_ps1()) {
    boxm2_volm_matcher_p1 obj_order_matcher(query, leaves, buffer_capacity(), geo_index_folder(), tile_id(),
                                            depth_interval, cand_poly, mgr->gpus_[dev_id()], is_candidate, is_last_pass, out_folder(),
                                            threshold(), max_cam_per_loc() ,use_orient());
    if (! obj_order_matcher.volm_matcher_p1()) {
      log << " ERROR: pass 1 volm_matcher failed for geo_index " << index_file << '\n';
      if (do_log) {
        volm_io::write_status(out_folder(), volm_io::MATCHER_EXE_FAILED);
        
      }
      vcl_cerr << log.str() << vcl_endl;
      return volm_io::MATCHER_EXE_FAILED;
    }
    // output will be a probability map
    vcl_cout << "\n==================================================================================================\n"
             << "\t\t  5. Generate output for pass 1 matcher and store it in\n"
             << "\t\t     " << out_folder() << '\n'
             << "==================================================================================================\n" << vcl_endl;
    vcl_stringstream out_fname_bin;
    out_fname_bin << out_folder() << "ps_1_scores_tile_" << tile_id() << ".bin";
    vcl_stringstream out_fname_txt;
    out_fname_txt << out_folder() << "ps_1_scores_tile_" << tile_id() << ".txt";
    if (!obj_order_matcher.write_matcher_result(out_fname_bin.str())) {
      log << " ERROR: writing output failed for pass 1 ray_based matcher\n";
      if (do_log) {
        volm_io::write_status(out_folder(), volm_io::MATCHER_EXE_FAILED);
        volm_io::write_log(out_folder(), log.str());
      }
      vcl_cerr << log.str() << vcl_endl;
      return volm_io::MATCHER_EXE_FAILED;
    }
    // output the camera score for desired ground truth location
    if (gt_out()) {
      vcl_stringstream gt_score_txt;
      gt_score_txt << out_folder() << "ps_1_gt_l_" << gt_l_id() << "_h_" << gt_h_id() << "_cam_scores.txt";
      if (!obj_order_matcher.write_gt_cam_score(gt_l_id(), gt_h_id(), gt_score_txt.str())) {
        log << " ERROR: writing output failed --> can not find ground truth leaf_id " << gt_l_id() << ", hypo_id " << gt_h_id() << '\n';
        if (do_log) {
          volm_io::write_status(out_folder(), volm_io::MATCHER_EXE_FAILED);
          volm_io::write_log(out_folder(), log.str());
        }
        vcl_cerr << log.str() << vcl_endl;
        return volm_io::MATCHER_EXE_FAILED;
      }
      vcl_cout << " ground truth score stored in " << gt_score_txt.str() << vcl_endl;
    }
  }
  else {
    vcl_cout << " object based depth/order matcher (pass 1) is avoided" << vcl_endl;
  }

  // start pass 2 matcher
  if (use_ps2()) {
    vcl_cout << " we will use pass 2, i.e. object based, ray based ORIENT/NLCD matcher\n"
             << " input: query, index, leaves, candidate list(is_candidate), depth_interval\n"
             << " NEED TO CHECK WHETHEER WE HAVE PASS 0 MATCHER RESULT, IF SO, LOAD THE REDUCED SPACE FROM PASS 0" << vcl_endl;
  }
  else {
    vcl_cout << " object based orientation/land classification matcher (pass 2) is avoided" << vcl_endl;
  }
#endif

#if 0
  // read the generated binary to check the value
  vcl_vector<volm_score_sptr> scores;
  vcl_stringstream out_fname_bin;
  out_fname_bin << out_folder() << "ps_1_scores_tile_" << tile_id() << ".bin";
  volm_score::read_scores(scores, out_fname_bin.str());  // this file may be too large, make sure it fits to memory!!
  vcl_cout << " THE READ IN BINRAY SCORE FILE " << vcl_endl;
  for (unsigned i = 0; i < scores.size(); i++) {
    vcl_cout << scores[i]->leaf_id_ << " " << scores[i]->hypo_id_
             << " " << scores[i]->max_score_ << " " << scores[i]->max_cam_id_ << vcl_endl;
    vcl_cout << " cam_id: \t";
    vcl_vector<unsigned> cam_ids = scores[i]->cam_id_;
    for (unsigned jj = 0; jj < cam_ids.size(); jj++)
      vcl_cout << ' ' << cam_ids[jj];
    vcl_cout << '\n';
  }
#endif

  // finish everything successfully
  volm_io::write_status(out_folder(), volm_io::MATCHER_EXE_FINISHED);
  return volm_io::SUCCESS;
}