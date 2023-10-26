/********************************************************************
 * Description: shcom.cc
 *   Common functions for NML calls
 *
 *   Derived from a work by Fred Proctor & Will Shackleford
 *   Further derived from work by jmkasunich, Alex Joni
 *
 * Author: Eric H. Johnson
 * License: GPL Version 2
 * System: Linux
 *
 * refactored (c) 2023 Robert Schöftner <rs@unfoo.net>
 *
 * Copyright (c) 2006 All rights reserved.
 *
 * Last change:
 ********************************************************************/

#define __STDC_FORMAT_MACROS
#include "shcom.hh" // Common NML communications functions

#include "canon.hh" // CANON_UNITS, CANON_UNITS_INCHES,MM,CM
#include "emc.hh"   // EMC NML
#include "emc_nml.hh"
#include "emccfg.h"   // DEFAULT_TRAJ_MAX_VELOCITY
#include "emcglb.h"   // EMC_NMLFILE, TRAJ_MAX_VELOCITY, etc.
#include "inifile.hh" // INIFILE
#include "nml_oi.hh"  // nmlErrorFormat, NML_ERROR, etc
#include "posemath.h" // PM_POSE, TO_RAD
#include "rcs.hh"
#include "rcs_print.hh"
#include "timer.hh" // esleep

#include <ctype.h>
#include <inttypes.h>
#include <math.h>
#include <rtapi_string.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <streambuf>
#include <string.h>
#include <string>
#include <sys/types.h>
#include <unistd.h>

std::string error_string;
std::string operator_text_string;
std::string operator_display_string;
char defaultPath[80] = DEFAULT_PATH;

int ShCom::emc_task_nml_get()
{
  // try to connect to EMC cmd
  if (m_command_buffer == nullptr) {
    m_command_buffer = std::make_unique<RCS_CMD_CHANNEL>(
        emcFormat, "emcCommand", "xemc", emc_nmlfile);
  }
  // try to connect to EMC status
  if (m_status_buffer == nullptr) {
    m_status_buffer = std::make_unique<RCS_STAT_CHANNEL>(emcFormat, "emcStatus",
                                                         "xemc", emc_nmlfile);
  }
  if (m_command_buffer == nullptr || m_status_buffer == nullptr)
    return -1;

  m_status = static_cast<EMC_STAT*>(m_status_buffer->get_address());

  return 0;
}

int ShCom::emc_error_nml_get()
{
  if (m_emc_error_buffer == nullptr) {
    m_emc_error_buffer =
        std::make_unique<NML>(nmlErrorFormat, "emcError", "xemc", emc_nmlfile);
  }
  if (m_emc_error_buffer == nullptr)
    return -1;
  return 0;
}

int ShCom::try_nml(double retry_time, double retry_interval)
{
  class InhibitDebugMsgGuard
  {
  public:
    InhibitDebugMsgGuard()
    {
      if ((emc_debug & EMC_DEBUG_NML) == 0) {
        // inhibit diag messages
        set_rcs_print_destination(RCS_PRINT_TO_NULL);
      }
    }
    ~InhibitDebugMsgGuard()
    {
      if ((emc_debug & EMC_DEBUG_NML) == 0) {
        // enable diag messages
        set_rcs_print_destination(RCS_PRINT_TO_STDOUT);
      }
    }
  };

  class Helper
  {
  public:
    static int retry_func(double retry_time, double retry_interval,
                          std::function<int()> func)
    {
      InhibitDebugMsgGuard guard;
      double end;
      int good;
      end = retry_time;
      good = 0;
      do {
        if (0 == func()) {
          good = 1;
          break;
        }
        esleep(retry_interval);
        end -= retry_interval;
      } while (end > 0.0);
      return good;
    }
  };

  return (!Helper::retry_func(retry_time, retry_interval, [&]() {
    return emc_task_nml_get();
  }) || !Helper::retry_func(retry_time, retry_interval, [&]() {
    return emc_error_nml_get();
  }));
}

int ShCom::update_status()
{
  NMLTYPE type;

  if (m_status == nullptr || !m_status_buffer->valid()) {
    return -1;
  }

  switch (type = m_status_buffer->peek()) {
  case -1:
    // error on CMS channel
    return -1;
    break;

  case 0:             // no new data
  case EMC_STAT_TYPE: // new data
    // new data
    break;

  default:
    return -1;
    break;
  }

  return 0;
}

/*
  updateError() updates "errors," which are true errors and also
  operator display and text messages.
*/
int ShCom::update_error()
{
  NMLTYPE type;

  if (!m_emc_error_buffer->valid()) {
    return -1;
  }

  switch (type = m_emc_error_buffer->read()) {
  case -1:
    // error reading channel
    return -1;
    break;

  case 0:
    // nothing new
    break;

  case EMC_OPERATOR_ERROR_TYPE:
    error_string =
        static_cast<EMC_OPERATOR_ERROR*>((m_emc_error_buffer->get_address()))
            ->error;
    break;

  case EMC_OPERATOR_TEXT_TYPE:
    operator_text_string =
        static_cast<EMC_OPERATOR_TEXT*>((m_emc_error_buffer->get_address()))
            ->text;
    break;

  case EMC_OPERATOR_DISPLAY_TYPE:
    operator_display_string =
        static_cast<EMC_OPERATOR_DISPLAY*>((m_emc_error_buffer->get_address()))
            ->display;
    break;

  case NML_ERROR_TYPE:
    error_string =
        static_cast<NML_ERROR*>((m_emc_error_buffer->get_address()))->error;
    break;

  case NML_TEXT_TYPE:
    operator_text_string =
        static_cast<NML_TEXT*>((m_emc_error_buffer->get_address()))->text;
    break;

  case NML_DISPLAY_TYPE:
    operator_display_string =
        static_cast<NML_DISPLAY*>((m_emc_error_buffer->get_address()))->display;
    break;

  default: {
    std::ostringstream buf;
    // if not recognized, set the error string
    buf << "unrecognized error type " << type;
    error_string = buf.str();
    return -1;
    break;
  }
  }
  return 0;
}

#define EMC_COMMAND_DELAY 0.1 // how long to sleep between checks

int ShCom::emc_command_wait_done()
{
  double end;
  for (end = 0.0; m_emc_timeout <= 0.0 || end < m_emc_timeout;
       end += EMC_COMMAND_DELAY)
  {
    update_status();
    int serial_diff = status().echo_serial_number - m_emc_command_serial_number;
    if (serial_diff < 0) {
      continue;
    }

    if (serial_diff > 0) {
      return 0;
    }

    if (status().status == RCS_DONE) {
      return 0;
    }

    if (status().status == RCS_ERROR) {
      return -1;
    }

    esleep(EMC_COMMAND_DELAY);
  }

  return -1;
}

int ShCom::emc_command_wait_received()
{
  double end;
  for (end = 0.0; m_emc_timeout <= 0.0 || end < m_emc_timeout;
       end += EMC_COMMAND_DELAY)
  {
    update_status();

    int serial_diff = status().echo_serial_number - m_emc_command_serial_number;
    if (serial_diff >= 0) {
      return 0;
    }

    esleep(EMC_COMMAND_DELAY);
  }

  return -1;
}

int ShCom::emc_command_wait()
{
  if (m_emc_wait_type == EMC_WAIT_TYPE::EMC_WAIT_RECEIVED) {
    return emc_command_wait_received();
  }
  else if (m_emc_wait_type == EMC_WAIT_TYPE::EMC_WAIT_DONE) {
    return emc_command_wait_done();
  }
  return -1;
}

int ShCom::emc_command_send(RCS_CMD_MSG& cmd)
{
  // write command
  if (m_command_buffer->write(&cmd)) {
    return -1;
  }
  m_emc_command_serial_number = cmd.serial_number;
  return 0;
}

int ShCom::emc_command_send_and_wait(RCS_CMD_MSG& cmd)
{
  return (ShCom::emc_command_send(cmd) || ShCom::emc_command_wait());
}

/*
  Unit conversion

  Length and angle units in the EMC status buffer are in user units, as
  defined in the INI file in [TRAJ] LINEAR,ANGULAR_UNITS. These may differ
  from the program units, and when they are the display is confusing.

  It may be desirable to synchronize the display units with the program
  units automatically, and also to break this sync and allow independent
  display of position values.

  The global variable "linearUnitConversion" is set by the Tcl commands
  emc_linear_unit_conversion to correspond to either "inch",
  "mm", "cm", "auto", or "custom". This forces numbers to be returned in the
  units specified, in program units when "auto" is set, or not converted
  at all if "custom" is specified.

  Ditto for "angularUnitConversion", set by emc_angular_unit_conversion
  to "deg", "rad", "grad", "auto", or "custom".

  With no args, emc_linear/angular_unit_conversion return the setting.

  The functions convertLinearUnits and convertAngularUnits take a length
  or angle value, typically from the emcStatus structure, and convert it
  as indicated by linearUnitConversion and angularUnitConversion, resp.
*/

/*
  to convert linear units, values are converted to mm, then to desired
  units
*/
double ShCom::convert_linear_units(double u)
{
  double in_mm;

  /* convert u to mm */
  in_mm = u / status().motion.traj.linearUnits;

  /* convert u to display units */
  switch (m_linear_unit_conversion) {
  case LINEAR_UNIT_CONVERSION::LINEAR_UNITS_MM:
    return in_mm;
    break;
  case LINEAR_UNIT_CONVERSION::LINEAR_UNITS_INCH:
    return in_mm * INCH_PER_MM;
    break;
  case LINEAR_UNIT_CONVERSION::LINEAR_UNITS_CM:
    return in_mm * CM_PER_MM;
    break;
  case LINEAR_UNIT_CONVERSION::LINEAR_UNITS_AUTO:
    switch (status().task.programUnits) {
    case CANON_UNITS::CANON_UNITS_MM:
      return in_mm;
      break;
    case CANON_UNITS::CANON_UNITS_INCHES:
      return in_mm * INCH_PER_MM;
      break;
    case CANON_UNITS::CANON_UNITS_CM:
      return in_mm * CM_PER_MM;
      break;
    }
    break;

  case LINEAR_UNIT_CONVERSION::LINEAR_UNITS_CUSTOM:
    return u;
    break;
  }

  // If it ever gets here we have an error.

  return u;
}

double ShCom::convert_angular_units(double u)
{
  // Angular units are always degrees
  return u;
}

int ShCom::send_debug(int level)
{
  EMC_SET_DEBUG debug_msg;

  debug_msg.debug = level;
  return emc_command_send_and_wait(debug_msg);
}

int ShCom::send_ESTOP()
{
  EMC_TASK_SET_STATE state_msg;

  state_msg.state = EMC_TASK_STATE_ESTOP;
  return emc_command_send_and_wait(state_msg);
}

int ShCom::send_ESTOP_reset()
{
  EMC_TASK_SET_STATE state_msg;

  state_msg.state = EMC_TASK_STATE_ESTOP_RESET;
  return emc_command_send_and_wait(state_msg);
}

int ShCom::send_machine_on()
{
  EMC_TASK_SET_STATE state_msg;

  state_msg.state = EMC_TASK_STATE_ON;
  return emc_command_send_and_wait(state_msg);
}

int ShCom::send_machhine_off()
{
  EMC_TASK_SET_STATE state_msg;

  state_msg.state = EMC_TASK_STATE_OFF;
  return emc_command_send_and_wait(state_msg);
}

int ShCom::send_manual()
{
  EMC_TASK_SET_MODE mode_msg;

  mode_msg.mode = EMC_TASK_MODE_MANUAL;
  return emc_command_send_and_wait(mode_msg);
}

int ShCom::send_auto()
{
  EMC_TASK_SET_MODE mode_msg;

  mode_msg.mode = EMC_TASK_MODE_AUTO;
  return emc_command_send_and_wait(mode_msg);
}

int ShCom::send_mdi()
{
  EMC_TASK_SET_MODE mode_msg;

  mode_msg.mode = EMC_TASK_MODE_MDI;
  return emc_command_send_and_wait(mode_msg);
}

int ShCom::send_override_limits(int joint)
{
  EMC_JOINT_OVERRIDE_LIMITS lim_msg;

  lim_msg.joint = joint; // neg means off, else on for all
  return emc_command_send_and_wait(lim_msg);
}

int ShCom::send_jog_stop(int ja, int jjogmode)
{
  EMC_JOG_STOP emc_jog_stop_msg;

  if (((jjogmode == JOGJOINT) &&
       (status().motion.traj.mode == EMC_TRAJ_MODE_TELEOP)) ||
      ((jjogmode == JOGTELEOP) &&
       (status().motion.traj.mode != EMC_TRAJ_MODE_TELEOP)))
  {
    return -1;
  }

  if (jjogmode && (ja < 0 || ja >= c_num_joints)) {
    fprintf(stderr, "shcom.cc: unexpected_1 %d\n", ja);
    return -1;
  }
  if (!jjogmode && (ja < 0)) {
    fprintf(stderr, "shcom.cc: unexpected_2 %d\n", ja);
    return -1;
  }

  emc_jog_stop_msg.jjogmode = jjogmode;
  emc_jog_stop_msg.joint_or_axis = ja;
  emc_command_send(emc_jog_stop_msg);
  return 0;
}

int ShCom::send_jog_cont(int ja, int jjogmode, double speed)
{
  EMC_JOG_CONT emc_jog_cont_msg;

  if (status().task.state != EMC_TASK_STATE_ON) {
    return -1;
  }
  if (((jjogmode == JOGJOINT) &&
       (status().motion.traj.mode == EMC_TRAJ_MODE_TELEOP)) ||
      ((jjogmode == JOGTELEOP) &&
       (status().motion.traj.mode != EMC_TRAJ_MODE_TELEOP)))
  {
    return -1;
  }

  if (jjogmode && (ja < 0 || ja >= c_num_joints)) {
    fprintf(stderr, "shcom.cc: unexpected_3 %d\n", ja);
    return -1;
  }
  if (!jjogmode && (ja < 0)) {
    fprintf(stderr, "shcom.cc: unexpected_4 %d\n", ja);
    return -1;
  }

  emc_jog_cont_msg.jjogmode = jjogmode;
  emc_jog_cont_msg.joint_or_axis = ja;
  emc_jog_cont_msg.vel = speed / 60.0;

  emc_command_send(emc_jog_cont_msg);

  return 0;
}

int ShCom::send_jog_incr(int ja, int jjogmode, double speed, double incr)
{
  EMC_JOG_INCR emc_jog_incr_msg;

  if (status().task.state != EMC_TASK_STATE_ON) {
    return -1;
  }
  if (((jjogmode == JOGJOINT) &&
       (status().motion.traj.mode == EMC_TRAJ_MODE_TELEOP)) ||
      ((jjogmode == JOGTELEOP) &&
       (status().motion.traj.mode != EMC_TRAJ_MODE_TELEOP)))
  {
    return -1;
  }

  if (jjogmode && (ja < 0 || ja >= c_num_joints)) {
    fprintf(stderr, "shcom.cc: unexpected_5 %d\n", ja);
    return -1;
  }
  if (!jjogmode && (ja < 0)) {
    fprintf(stderr, "shcom.cc: unexpected_6 %d\n", ja);
    return -1;
  }

  emc_jog_incr_msg.jjogmode = jjogmode;
  emc_jog_incr_msg.joint_or_axis = ja;
  emc_jog_incr_msg.vel = speed / 60.0;
  emc_jog_incr_msg.incr = incr;

  emc_command_send(emc_jog_incr_msg);

  return 0;
}

int ShCom::send_mist_on()
{
  EMC_COOLANT_MIST_ON emc_coolant_mist_on_msg;

  return emc_command_send_and_wait(emc_coolant_mist_on_msg);
}

int ShCom::send_mist_off()
{
  EMC_COOLANT_MIST_OFF emc_coolant_mist_off_msg;

  return emc_command_send_and_wait(emc_coolant_mist_off_msg);
}

int ShCom::send_flood_on()
{
  EMC_COOLANT_FLOOD_ON emc_coolant_flood_on_msg;

  return emc_command_send_and_wait(emc_coolant_flood_on_msg);
}

int ShCom::send_flood_off()
{
  EMC_COOLANT_FLOOD_OFF emc_coolant_flood_off_msg;

  return emc_command_send_and_wait(emc_coolant_flood_off_msg);
}

int ShCom::send_lube_on()
{
  EMC_LUBE_ON emc_lube_on_msg;

  return emc_command_send_and_wait(emc_lube_on_msg);
}

int ShCom::send_lube_off()
{
  EMC_LUBE_OFF emc_lube_off_msg;

  return emc_command_send_and_wait(emc_lube_off_msg);
}

int ShCom::send_spindle_forward(int spindle)
{
  EMC_SPINDLE_ON emc_spindle_on_msg;
  emc_spindle_on_msg.spindle = spindle;
  if (status().task.activeSettings[2] != 0) {
    emc_spindle_on_msg.speed = fabs(status().task.activeSettings[2]);
  }
  else {
    emc_spindle_on_msg.speed = +500;
  }
  return emc_command_send_and_wait(emc_spindle_on_msg);
}

int ShCom::send_spindle_reverse(int spindle)
{
  EMC_SPINDLE_ON emc_spindle_on_msg;
  emc_spindle_on_msg.spindle = spindle;
  if (status().task.activeSettings[2] != 0) {
    emc_spindle_on_msg.speed = -1 * fabs(status().task.activeSettings[2]);
  }
  else {
    emc_spindle_on_msg.speed = -500;
  }
  return emc_command_send_and_wait(emc_spindle_on_msg);
}

int ShCom::send_spindle_off(int spindle)
{
  EMC_SPINDLE_OFF emc_spindle_off_msg;
  emc_spindle_off_msg.spindle = spindle;
  return emc_command_send_and_wait(emc_spindle_off_msg);
}

int ShCom::send_spindle_increase(int spindle)
{
  EMC_SPINDLE_INCREASE emc_spindle_increase_msg;
  emc_spindle_increase_msg.spindle = spindle;
  return emc_command_send_and_wait(emc_spindle_increase_msg);
}

int ShCom::send_spindle_decrease(int spindle)
{
  EMC_SPINDLE_DECREASE emc_spindle_decrease_msg;
  emc_spindle_decrease_msg.spindle = spindle;
  return emc_command_send_and_wait(emc_spindle_decrease_msg);
}

int ShCom::send_spindle_constant(int spindle)
{
  EMC_SPINDLE_CONSTANT emc_spindle_constant_msg;
  emc_spindle_constant_msg.spindle = spindle;
  return emc_command_send_and_wait(emc_spindle_constant_msg);
}

int ShCom::send_spindle_brake_engage(int spindle)
{
  EMC_SPINDLE_BRAKE_ENGAGE emc_spindle_brake_engage_msg;

  emc_spindle_brake_engage_msg.spindle = spindle;
  return emc_command_send_and_wait(emc_spindle_brake_engage_msg);
}

int ShCom::send_spindle_brake_release(int spindle)
{
  EMC_SPINDLE_BRAKE_RELEASE emc_spindle_brake_release_msg;

  emc_spindle_brake_release_msg.spindle = spindle;
  return emc_command_send_and_wait(emc_spindle_brake_release_msg);
}

int ShCom::send_abort()
{
  EMC_TASK_ABORT task_abort_msg;

  return emc_command_send_and_wait(task_abort_msg);
}

int ShCom::send_home(int joint)
{
  EMC_JOINT_HOME emc_joint_home_msg;

  emc_joint_home_msg.joint = joint;
  return emc_command_send_and_wait(emc_joint_home_msg);
}

int ShCom::send_un_home(int joint)
{
  EMC_JOINT_UNHOME emc_joint_home_msg;

  emc_joint_home_msg.joint = joint;
  return emc_command_send_and_wait(emc_joint_home_msg);
}

int ShCom::send_feed_override(double override)
{
  EMC_TRAJ_SET_SCALE emc_traj_set_scale_msg;

  if (override < 0.0) {
    override = 0.0;
  }

  emc_traj_set_scale_msg.scale = override;
  return emc_command_send_and_wait(emc_traj_set_scale_msg);
}

int ShCom::send_rapid_override(double override)
{
  EMC_TRAJ_SET_RAPID_SCALE emc_traj_set_scale_msg;

  if (override < 0.0) {
    override = 0.0;
  }

  if (override > 1.0) {
    override = 1.0;
  }

  emc_traj_set_scale_msg.scale = override;
  return emc_command_send_and_wait(emc_traj_set_scale_msg);
}

int ShCom::send_spindle_override(int spindle, double override)
{
  EMC_TRAJ_SET_SPINDLE_SCALE emc_traj_set_spindle_scale_msg;

  if (override < 0.0) {
    override = 0.0;
  }

  emc_traj_set_spindle_scale_msg.spindle = spindle;
  emc_traj_set_spindle_scale_msg.scale = override;
  return emc_command_send_and_wait(emc_traj_set_spindle_scale_msg);
}

int ShCom::send_task_plan_init()
{
  EMC_TASK_PLAN_INIT task_plan_init_msg;

  return emc_command_send_and_wait(task_plan_init_msg);
}

// saved value of last program opened
static char lastProgramFile[LINELEN] = "";

int ShCom::send_program_open(char* program)
{
  EMC_TASK_PLAN_OPEN emc_task_plan_open_msg;

  // save this to run again
  rtapi_strxcpy(lastProgramFile, program);

  rtapi_strxcpy(emc_task_plan_open_msg.file, program);
  return emc_command_send_and_wait(emc_task_plan_open_msg);
}

int ShCom::send_program_run(int line)
{
  EMC_TASK_PLAN_RUN emc_task_plan_run_msg;

  if (m_emc_update_type == EMC_UPDATE_TYPE::EMC_UPDATE_AUTO) {
    update_status();
  }
  // first reopen program if it's not open
  if (0 == status().task.file[0]) {
    // send a request to open last one
    send_program_open(lastProgramFile);
  }
  // save the start line, to compare against active line later
  m_program_start_line = line;

  emc_task_plan_run_msg.line = line;
  return emc_command_send_and_wait(emc_task_plan_run_msg);
}

int ShCom::send_program_pause()
{
  EMC_TASK_PLAN_PAUSE emc_task_plan_pause_msg;

  return emc_command_send_and_wait(emc_task_plan_pause_msg);
}

int ShCom::send_program_resume()
{
  EMC_TASK_PLAN_RESUME emc_task_plan_resume_msg;

  return emc_command_send_and_wait(emc_task_plan_resume_msg);
}

int ShCom::send_set_optional_stop(bool state)
{
  EMC_TASK_PLAN_SET_OPTIONAL_STOP emc_task_plan_set_optional_stop_msg;

  emc_task_plan_set_optional_stop_msg.state = state;
  return emc_command_send_and_wait(emc_task_plan_set_optional_stop_msg);
}

int ShCom::send_program_step()
{
  EMC_TASK_PLAN_STEP emc_task_plan_step_msg;

  // clear out start line, if we had a verify before it would be -1
  m_program_start_line = 0;

  return emc_command_send_and_wait(emc_task_plan_step_msg);
}

int ShCom::send_mdi_cmd(const char* mdi)
{
  EMC_TASK_PLAN_EXECUTE emc_task_plan_execute_msg;

  rtapi_strxcpy(emc_task_plan_execute_msg.command, mdi);
  return emc_command_send_and_wait(emc_task_plan_execute_msg);
}

int ShCom::send_load_tool_table(const char* file)
{
  EMC_TOOL_LOAD_TOOL_TABLE emc_tool_load_tool_table_msg;

  rtapi_strxcpy(emc_tool_load_tool_table_msg.file, file);
  return emc_command_send_and_wait(emc_tool_load_tool_table_msg);
}

int ShCom::send_tool_set_offset(int toolno, double zoffset, double diameter)
{
  EMC_TOOL_SET_OFFSET emc_tool_set_offset_msg;

  emc_tool_set_offset_msg.toolno = toolno;
  emc_tool_set_offset_msg.offset.tran.z = zoffset;
  emc_tool_set_offset_msg.diameter = diameter;
  emc_tool_set_offset_msg.orientation = 0; // mill style tool table

  return emc_command_send_and_wait(emc_tool_set_offset_msg);
}

int ShCom::send_tool_set_offset(int toolno, double zoffset, double xoffset,
                                double diameter, double frontangle,
                                double backangle, int orientation)
{
  EMC_TOOL_SET_OFFSET emc_tool_set_offset_msg;

  emc_tool_set_offset_msg.toolno = toolno;
  emc_tool_set_offset_msg.offset.tran.z = zoffset;
  emc_tool_set_offset_msg.offset.tran.x = xoffset;
  emc_tool_set_offset_msg.diameter = diameter;
  emc_tool_set_offset_msg.frontangle = frontangle;
  emc_tool_set_offset_msg.backangle = backangle;
  emc_tool_set_offset_msg.orientation = orientation;

  return emc_command_send_and_wait(emc_tool_set_offset_msg);

  return 0;
}

int ShCom::send_joint_set_backlash(int joint, double backlash)
{
  EMC_JOINT_SET_BACKLASH emc_joint_set_backlash_msg;

  emc_joint_set_backlash_msg.joint = joint;
  emc_joint_set_backlash_msg.backlash = backlash;
  return emc_command_send_and_wait(emc_joint_set_backlash_msg);
}

int ShCom::send_joint_enable(int joint, int val)
{
  EMC_JOINT_ENABLE emc_joint_enable_msg;
  EMC_JOINT_DISABLE emc_joint_disable_msg;

  if (val) {
    emc_joint_enable_msg.joint = joint;
    return emc_command_send_and_wait(emc_joint_enable_msg);
  }
  else {
    emc_joint_disable_msg.joint = joint;
    return emc_command_send_and_wait(emc_joint_disable_msg);
  }
}

int ShCom::send_joint_load_comp(int joint, const char* file, int type)
{
  EMC_JOINT_LOAD_COMP emc_joint_load_comp_msg;

  rtapi_strxcpy(emc_joint_load_comp_msg.file, file);
  emc_joint_load_comp_msg.type = type;
  return emc_command_send_and_wait(emc_joint_load_comp_msg);
}

int ShCom::send_set_teleop_enable(int enable)
{
  EMC_TRAJ_SET_TELEOP_ENABLE emc_set_teleop_enable_msg;

  emc_set_teleop_enable_msg.enable = enable;
  return emc_command_send_and_wait(emc_set_teleop_enable_msg);
}

int ShCom::send_clear_probe_tripped_flag()
{
  EMC_TRAJ_CLEAR_PROBE_TRIPPED_FLAG emc_clear_probe_tripped_flag_msg;

  emc_clear_probe_tripped_flag_msg.serial_number =
      emc_command_send(emc_clear_probe_tripped_flag_msg);
  return emc_command_wait();
}

int ShCom::send_probe(double x, double y, double z)
{
  EMC_TRAJ_PROBE emc_probe_msg;

  emc_probe_msg.pos.tran.x = x;
  emc_probe_msg.pos.tran.y = y;
  emc_probe_msg.pos.tran.z = z;

  return emc_command_send_and_wait(emc_probe_msg);
}

int ShCom::ini_load(const char* filename)
{
  IniFile inifile;
  const char* inistring;
  char displayString[LINELEN] = "";
  int t;
  int i;

  // open it
  if (inifile.Open(filename) == false) {
    return -1;
  }

  if (NULL != (inistring = inifile.Find("DEBUG", "EMC"))) {
    // copy to global
    if (1 != sscanf(inistring, "%i", &emc_debug)) {
      emc_debug = 0;
    }
  }
  else {
    // not found, use default
    emc_debug = 0;
  }

  if (NULL != (inistring = inifile.Find("NML_FILE", "EMC"))) {
    // copy to global
    rtapi_strxcpy(emc_nmlfile, inistring);
  }
  else {
    // not found, use default
  }

  for (t = 0; t < EMCMOT_MAX_JOINTS; t++) {
    m_jog_pol[t] = 1; // set to default
    snprintf(displayString, sizeof(displayString), "JOINT_%d", t);
    if (NULL != (inistring = inifile.Find("JOGGING_POLARITY", displayString)) &&
        1 == sscanf(inistring, "%d", &i) && i == 0)
    {
      // it read as 0, so override default
      m_jog_pol[t] = 0;
    }
  }

  if (NULL != (inistring = inifile.Find("LINEAR_UNITS", "DISPLAY"))) {
    if (!strcmp(inistring, "AUTO")) {
      m_linear_unit_conversion = LINEAR_UNIT_CONVERSION::LINEAR_UNITS_AUTO;
    }
    else if (!strcmp(inistring, "INCH")) {
      m_linear_unit_conversion = LINEAR_UNIT_CONVERSION::LINEAR_UNITS_INCH;
    }
    else if (!strcmp(inistring, "MM")) {
      m_linear_unit_conversion = LINEAR_UNIT_CONVERSION::LINEAR_UNITS_MM;
    }
    else if (!strcmp(inistring, "CM")) {
      m_linear_unit_conversion = LINEAR_UNIT_CONVERSION::LINEAR_UNITS_CM;
    }
  }
  else {
    // not found, leave default alone
  }

  if (NULL != (inistring = inifile.Find("ANGULAR_UNITS", "DISPLAY"))) {
    if (!strcmp(inistring, "AUTO")) {
      m_angular_unit_conversion = ANGULAR_UNIT_CONVERSION::ANGULAR_UNITS_AUTO;
    }
    else if (!strcmp(inistring, "DEG")) {
      m_angular_unit_conversion = ANGULAR_UNIT_CONVERSION::ANGULAR_UNITS_DEG;
    }
    else if (!strcmp(inistring, "RAD")) {
      m_angular_unit_conversion = ANGULAR_UNIT_CONVERSION::ANGULAR_UNITS_RAD;
    }
    else if (!strcmp(inistring, "GRAD")) {
      m_angular_unit_conversion = ANGULAR_UNIT_CONVERSION::ANGULAR_UNITS_GRAD;
    }
  }
  else {
    // not found, leave default alone
  }

  if (nullptr != (inistring = inifile.Find("EMCIO", "TOOL_TABLE"))) {
    m_tool_table_filename = inistring;
  }

  if (nullptr != (inistring = inifile.Find("RS274NGC", "PARAMETER_FILE"))) {
    m_parameter_filename = inistring;
  }

  // close it
  inifile.Close();

  return 0;
}

int ShCom::check_status()
{
  if (m_status)
    return 1;
  return 0;
}
