
#include <cstdlib>
#include <iostream>
#include <math.h>
#include "BHV_FollowCurrent.h"
#include "MBUtils.h"
#include "AngleUtils.h"
#include "BuildUtils.h"
#include "ZAIC_PEAK.h"
#include "OF_Coupler.h"

using namespace std;

//-----------------------------------------------------------
// Procedure: Constructor

BHV_SimpleWaypoint::BHV_SimpleWaypoint(IvPDomain gdomain) : 
  IvPBehavior(gdomain)
{
  IvPBehavior::setParam("name", "simple_waypoint");
  m_domain = subDomain(m_domain, "course,speed");

  // All distances are in meters, all speed in meters per second
  // Default values for configuration parameters 
  m_desired_speed  = 0; 
  m_arrival_radius = 10;
  m_ipf_type       = "zaic";

  // Default values for behavior state variables
  m_osx  = 0;
  m_osy  = 0;

  addInfoVars("NAV_X, NAV_Y");
  cout << "constructed" << endl;
}

//---------------------------------------------------------------
// Procedure: setParam - handle behavior configuration parameters

bool BHV_SimpleWaypoint::setParam(string param, string val) 
{
  // Convert the parameter to lower case for more general matching
  param = tolower(param);

  double double_val = atof(val.c_str());
  if((param == "ptx")  && (isNumber(val))) {
    m_nextpt.set_vx(double_val);
    return(true);
  }
  else if((param == "pty") && (isNumber(val))) {
    m_nextpt.set_vy(double_val);
    return(true);
  }
  else if((param == "speed") && (double_val > 0) && (isNumber(val))) {
    m_desired_speed = double_val;
    return(true);
  }
  else if((param == "radius") && (double_val > 0) && (isNumber(val))) {
    m_arrival_radius = double_val;
    return(true);
  }
  else if(param == "ipf_type") {
    val = tolower(val);    
    if((val == "zaic") || (val == "reflector")) {
      m_ipf_type = val;
      return(true);
    }
  }
  return(false);
}

//-----------------------------------------------------------
// Procedure: onIdleState

void BHV_SimpleWaypoint::onIdleState() 
{
  postViewPoint(false);
}

//-----------------------------------------------------------
// Procedure: postViewPoint

void BHV_SimpleWaypoint::postViewPoint(bool viewable) 
{
  m_nextpt.set_label(m_us_name + "'s next waypoint");
  
  string point_spec;
  if(viewable)
    point_spec = m_nextpt.get_spec("active=true");
  else
    point_spec = m_nextpt.get_spec("active=false");
  postMessage("VIEW_POINT", point_spec);
}

//-----------------------------------------------------------
// Procedure: onRunState

IvPFunction *BHV_SimpleWaypoint::onRunState() 
{
  // Part 1: Get vehicle position from InfoBuffer and post a 
  // warning if problem is encountered
  bool ok1, ok2;
  m_osx = getBufferDoubleVal("NAV_X", ok1);
  m_osy = getBufferDoubleVal("NAV_Y", ok2);
  if(!ok1 || !ok2) {
    postWMessage("No ownship X/Y info in info_buffer.");
    return(0);
  }
  
  // Part 2: Determine if the vehicle has reached the destination 
  // point and if so, declare completion.
#ifdef WIN32
  double dist = _hypot((m_nextpt.x()-m_osx), (m_nextpt.y()-m_osy));
#else
  double dist = hypot((m_nextpt.x()-m_osx), (m_nextpt.y()-m_osy));
#endif
  if(dist <= m_arrival_radius) {
    setComplete();
    postViewPoint(false);
    return(0);
  }

  // Part 3: Post the waypoint as a string for consumption by 
  // a viewer application.
  postViewPoint(true);

  // Part 4: Build the IvP function with either the ZAIC tool 
  // or the Reflector tool.
  IvPFunction *ipf = 0;
  if(m_ipf_type == "zaic")
    ipf = buildFunctionWithZAIC();
  if(ipf == 0) 
    postWMessage("Problem Creating the IvP Function");
  if(ipf)
    ipf->setPWT(m_priority_wt);
  
  return(ipf);
}

//-----------------------------------------------------------
// Procedure: buildFunctionWithZAIC

IvPFunction *BHV_SimpleWaypoint::buildFunctionWithZAIC() 
{
  ZAIC_PEAK spd_zaic(m_domain, "speed");
  spd_zaic.setSummit(m_desired_speed);
  spd_zaic.setPeakWidth(0.5);
  spd_zaic.setBaseWidth(1.0);
  spd_zaic.setSummitDelta(0.8);  
  if(spd_zaic.stateOK() == false) {
    string warnings = "Speed ZAIC problems " + spd_zaic.getWarnings();
    postWMessage(warnings);
    return(0);
  }
  
  double rel_ang_to_wpt = relAng(m_osx, m_osy, m_nextpt.x(), m_nextpt.y());
  ZAIC_PEAK crs_zaic(m_domain, "course");
  crs_zaic.setSummit(rel_ang_to_wpt);
  crs_zaic.setPeakWidth(0);
  crs_zaic.setBaseWidth(180.0);
  crs_zaic.setSummitDelta(0);  
  crs_zaic.setValueWrap(true);
  if(crs_zaic.stateOK() == false) {
    string warnings = "Course ZAIC problems " + crs_zaic.getWarnings();
    postWMessage(warnings);
    return(0);
  }

  IvPFunction *spd_ipf = spd_zaic.extractIvPFunction();
  IvPFunction *crs_ipf = crs_zaic.extractIvPFunction();

  OF_Coupler coupler;
  IvPFunction *ivp_function = coupler.couple(crs_ipf, spd_ipf, 50, 50);

  return(ivp_function);
}

