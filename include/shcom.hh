/********************************************************************
 * Description: shcom.hh
 *   Headers for common functions for NML calls
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
 * Copyright (c) 2007 All rights reserved.
 *
 * Last change:
 ********************************************************************/

#pragma once

#include "emc_nml.hh"
#include "linuxcnc.h" // INCH_PER_MM
#include "nml_oi.hh"  // NML_ERROR_LEN

#include <array>
#include <memory>

#define CLOSE(a, b, eps) ((a) - (b) < +(eps) && (a) - (b) > -(eps))
#define LINEAR_CLOSENESS 0.0001
#define ANGULAR_CLOSENESS 0.0001
#define CM_PER_MM 0.1
#define GRAD_PER_DEG (100.0 / 90.0)
#define RAD_PER_DEG TO_RAD // from posemath.h
#define DEFAULT_PATH "../../nc_files/"

#define JOGTELEOP 0
#define JOGJOINT 1

class ShCom
{
  static const int c_num_joints = EMCMOT_MAX_JOINTS;

public:
  enum class LINEAR_UNIT_CONVERSION {
    LINEAR_UNITS_CUSTOM = 1,
    LINEAR_UNITS_AUTO,
    LINEAR_UNITS_MM,
    LINEAR_UNITS_INCH,
    LINEAR_UNITS_CM

  };
  enum ANGULAR_UNIT_CONVERSION {
    ANGULAR_UNITS_CUSTOM = 1,
    ANGULAR_UNITS_AUTO,
    ANGULAR_UNITS_DEG,
    ANGULAR_UNITS_RAD,
    ANGULAR_UNITS_GRAD
  };
  enum class EMC_UPDATE_TYPE { EMC_UPDATE_NONE = 1, EMC_UPDATE_AUTO };
  enum class EMC_WAIT_TYPE { EMC_WAIT_RECEIVED = 2, EMC_WAIT_DONE };

  EMC_STAT& status() { return *m_status; }
  const EMC_STAT& status() const { return *m_status; }

  int emc_task_nml_get();
  int emc_error_nml_get();
  int try_nml(double retry_time = 10.0, double retry_interval = 1.0);
  int update_status();
  int update_error();

  int emc_command_wait_received();
  int emc_command_wait_done();
  int emc_command_wait();
  int emc_command_send(RCS_CMD_MSG& cmd);
  int emc_command_send_and_wait(RCS_CMD_MSG& cmd);

  double convert_linear_units(double u);
  double convert_angular_units(double u);

  int send_debug(int level);
  int send_ESTOP();
  int send_ESTOP_reset();
  int send_machine_on();
  int send_machhine_off();
  int send_manual();
  int send_auto();
  int send_mdi();
  int send_override_limits(int joint);
  int send_jog_stop(int ja, int jjogmode);
  int send_jog_cont(int ja, int jjogmode, double speed);
  int send_jog_incr(int ja, int jjogmode, double speed, double incr);
  int send_mist_on();
  int send_mist_off();
  int send_flood_on();
  int send_flood_off();
  int send_lube_on();
  int send_lube_off();
  int send_spindle_forward(int spindle);
  int send_spindle_reverse(int spindle);
  int send_spindle_off(int spindle);
  int send_spindle_increase(int spindle);
  int send_spindle_decrease(int spindle);
  int send_spindle_constant(int spindle);
  int send_spindle_brake_engage(int spindle);
  int send_spindle_brake_release(int spindle);
  int send_abort();
  int send_home(int joint);
  int send_un_home(int joint);
  int send_feed_override(double override);
  int send_rapid_override(double override);
  int send_spindle_override(int spindle, double override);
  int send_task_plan_init();
  int send_program_open(char* program);
  int send_program_run(int line);
  int send_program_pause();
  int send_program_resume();
  int send_set_optional_stop(bool state);
  int send_program_step();
  int send_mdi_cmd(const char* mdi);
  int send_load_tool_table(const char* file);
  int send_tool_set_offset(int toolno, double zoffset, double diameter);
  int send_tool_set_offset(int toolno, double zoffset, double xoffset,
                           double diameter, double frontangle, double backangle,
                           int orientation);
  int send_joint_set_backlash(int joint, double backlash);
  int send_joint_enable(int joint, int val);
  int send_joint_load_comp(int joint, const char* file, int type);
  int send_set_teleop_enable(int enable);
  int send_clear_probe_tripped_flag();
  int send_probe(double x, double y, double z);
  int ini_load(const char* filename);
  int check_status();

private:
  LINEAR_UNIT_CONVERSION m_linear_unit_conversion;
  ANGULAR_UNIT_CONVERSION m_angular_unit_conversion;

  // default value for timeout, 0 means wait forever
  double m_emc_timeout;
  EMC_UPDATE_TYPE m_emc_update_type;
  EMC_WAIT_TYPE m_emc_wait_type;
  EMC_STAT* m_status;

  // the current command number
  int m_emc_command_serial_number;
  int m_program_start_line;

  // polarities for joint jogging, from ini file
  std::array<int, EMCMOT_MAX_JOINTS> m_jog_pol;

  // the NML channels to the EMC task
  std::unique_ptr<RCS_CMD_CHANNEL> m_command_buffer;
  std::unique_ptr<RCS_STAT_CHANNEL> m_status_buffer;

  // the NML channel for errors
  std::unique_ptr<NML> m_emc_error_buffer;

  std::string m_parameter_filename;
  std::string m_tool_table_filename;
};

extern std::string error_string;
extern std::string operator_text_string;
extern std::string operator_display_string;
extern char defaultPath[80];
