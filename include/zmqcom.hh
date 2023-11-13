//
// zmqcom.hh
//
// (c) 2023 Robert Schöftner <rs@unfoo.net>
//
// GNU GPL v2
//

#pragma once

#include "emcmotcfg.h"
#include "emc.hh"

#include <array>
#include <memory>

// forward decls
class EMC_STAT;
namespace flatbuffers { class FlatBufferBuilder; }
namespace zmq { class socket_t; }
namespace EMC { class EmcStatT; }

class ZMQCom
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

  ZMQCom();
  ~ZMQCom();

  EMC::EmcStatT& status();
  const EMC::EmcStatT& status() const;

  void init();
  int update_status();
  int update_error();

  int emc_command_wait_received();
  int emc_command_wait_done();
  int emc_command_wait();
  int emc_command_send(flatbuffers::FlatBufferBuilder& fbb);
  int emc_command_send_and_wait(flatbuffers::FlatBufferBuilder& fbb);

  double convert_linear_units(double u);
  double convert_angular_units(double u);

  int send_debug(int level);
  int send_ESTOP();
  int send_ESTOP_reset();
  int send_machine_on();
  int send_machine_off();
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

private:
  int _send_task_set_state(EMC_TASK_STATE state);
  int _send_task_set_mode(EMC_TASK_MODE mode);
  template <typename s>
  int _send_simple_command();
  template <typename s, class... Args>
  int _send_command(Args&&... args);
  LINEAR_UNIT_CONVERSION m_linear_unit_conversion;
  ANGULAR_UNIT_CONVERSION m_angular_unit_conversion;

  // polarities for joint jogging, from ini file
  std::array<int, EMCMOT_MAX_JOINTS> m_jog_pol;

  std::unique_ptr<zmq::socket_t> command_socket;
  std::unique_ptr<zmq::socket_t> error_socket;
  std::unique_ptr<zmq::socket_t> status_socket;
  std::unique_ptr<EMC::EmcStatT> m_status;
};
