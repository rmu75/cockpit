//
// zmqcom.hh
//
// (c) 2023 Robert Schöftner <rs@unfoo.net>
//
// GNU GPL v2
//

#include <zmq.hpp>
#include <flatbuffers/flatbuffers.h>

#include "emc.hh"
#include "zmqcom.hh"

#include "../flatbuf/emc_cmd_generated.h"
#include "../flatbuf/emc_common_generated.h"
#include "../flatbuf/emc_error_generated.h"
#include "../flatbuf/emc_stat_generated.h"

static zmq::context_t context;

ZMQCom::ZMQCom() = default;

ZMQCom::~ZMQCom() = default;

void ZMQCom::init()
{
  // set up 0mq socket
  command_socket =
      std::make_unique<zmq::socket_t>(context, zmq::socket_type::push);
  command_socket->bind("ipc://@/tmp/linuxcnc-command");
  command_socket->connect("tcp://127.0.0.1:5027");

  status_socket =
      std::make_unique<zmq::socket_t>(context, zmq::socket_type::sub);
  status_socket->bind("ipc://@/tmp/linuxcnc-status");
  status_socket->connect("tcp://127.0.0.1:5028");
  status_socket->set(zmq::sockopt::subscribe, "");
  status_socket->set(zmq::sockopt::rcvhwm, 1);
  // only keep last status update
  // att, conflate is not compatible with subscription filters
  status_socket->set(zmq::sockopt::conflate, 1);

  error_socket =
      std::make_unique<zmq::socket_t>(context, zmq::socket_type::sub);
  error_socket->bind("ipc://@/tmp/linuxcnc-error");
  error_socket->connect("tcp://127.0.0.1:5029");
}


int ZMQCom::emc_command_send(flatbuffers::FlatBufferBuilder& fbb)
{
    zmq::message_t message(fbb.GetBufferPointer(), fbb.GetSize());
    command_socket->send(message, zmq::send_flags::dontwait);
    return 0;
}

int ZMQCom::emc_command_send_and_wait(flatbuffers::FlatBufferBuilder& fbb)
{
    zmq::message_t message(fbb.GetBufferPointer(), fbb.GetSize());
    command_socket->send(message, zmq::send_flags::none);
    return 0;
}

int ZMQCom::send_debug(int level) { return -1; }

void ZMQCom::_send_task_set_state(EMC_TASK_STATE state)
{
  flatbuffers::FlatBufferBuilder fbb;
  auto cmd = fbb.CreateStruct(EMC::TaskSetState(static_cast<int>(state)));
  auto msg = EMC::CmdChannelMsgBuilder(fbb);
  msg.add_command(cmd.Union());
  msg.add_command_type(EMC::Command_task_set_state);
  fbb.Finish(msg.Finish());
  emc_command_send(fbb);
}

int ZMQCom::send_ESTOP()
{
  _send_task_set_state(EMC_TASK_STATE::ESTOP);
  return 0;
}

int ZMQCom::send_ESTOP_reset()
{
  _send_task_set_state(EMC_TASK_STATE::ESTOP_RESET);
  return 0;
}

int ZMQCom::send_machine_on()
{
  _send_task_set_state(EMC_TASK_STATE::ON);
  return 0;
}

int ZMQCom::send_machine_off()
{
  _send_task_set_state(EMC_TASK_STATE::OFF);
  return 0;
}

int ZMQCom::send_manual() { return -1; }
  int ZMQCom::send_auto() { return -1; }
  int ZMQCom::send_mdi() { return -1; }
  int ZMQCom::send_override_limits(int joint) { return -1; }
  int ZMQCom::send_jog_stop(int ja, int jjogmode) { return -1; }
  int ZMQCom::send_jog_cont(int ja, int jjogmode, double speed) { return -1; }
  int ZMQCom::send_jog_incr(int ja, int jjogmode, double speed, double incr) { return -1; }
  int ZMQCom::send_mist_on() { return -1; }
  int ZMQCom::send_mist_off() { return -1; }
  int ZMQCom::send_flood_on() { return -1; }
  int ZMQCom::send_flood_off() { return -1; }
  int ZMQCom::send_lube_on() { return -1; }
  int ZMQCom::send_lube_off() { return -1; }
  int ZMQCom::send_spindle_forward(int spindle) { return -1; }
  int ZMQCom::send_spindle_reverse(int spindle) { return -1; }
  int ZMQCom::send_spindle_off(int spindle) { return -1; }
  int ZMQCom::send_spindle_increase(int spindle) { return -1; }
  int ZMQCom::send_spindle_decrease(int spindle) { return -1; }
  int ZMQCom::send_spindle_constant(int spindle) { return -1; }
  int ZMQCom::send_spindle_brake_engage(int spindle) { return -1; }
  int ZMQCom::send_spindle_brake_release(int spindle) { return -1; }
  int ZMQCom::send_abort() { return -1; }
  int ZMQCom::send_home(int joint) { return -1; }
  int ZMQCom::send_un_home(int joint) { return -1; }
  int ZMQCom::send_feed_override(double override) { return -1; }
  int ZMQCom::send_rapid_override(double override) { return -1; }
  int ZMQCom::send_spindle_override(int spindle, double override) { return -1; }
  int ZMQCom::send_task_plan_init() { return -1; }
  int ZMQCom::send_program_open(char* program) { return -1; }
  int ZMQCom::send_program_run(int line) { return -1; }
  int ZMQCom::send_program_pause() { return -1; }
  int ZMQCom::send_program_resume() { return -1; }
  int ZMQCom::send_set_optional_stop(bool state) { return -1; }
  int ZMQCom::send_program_step() { return -1; }
  int ZMQCom::send_mdi_cmd(const char* mdi) { return -1; }
  int ZMQCom::send_load_tool_table(const char* file) { return -1; }
  int ZMQCom::send_tool_set_offset(int toolno, double zoffset, double diameter) { return -1; }
  int ZMQCom::send_tool_set_offset(int toolno, double zoffset, double xoffset,
                           double diameter, double frontangle, double backangle,
                           int orientation) { return -1; }
  int ZMQCom::send_joint_set_backlash(int joint, double backlash) { return -1; }
  int ZMQCom::send_joint_enable(int joint, int val) { return -1; }
  int ZMQCom::send_joint_load_comp(int joint, const char* file, int type) { return -1; }
  int ZMQCom::send_set_teleop_enable(int enable) { return -1; }
  int ZMQCom::send_clear_probe_tripped_flag() { return -1; }
  int ZMQCom::send_probe(double x, double y, double z) { return -1; }
  int ZMQCom::ini_load(const char* filename) { return -1; }
