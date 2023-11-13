//
// zmqcom.hh
//
// (c) 2023 Robert Schöftner <rs@unfoo.net>
//
// GNU GPL v2
//

#include "zmqcom.hh"

#include "../flatbuf/emc_cmd_generated.h"
#include "../flatbuf/emc_common_generated.h"
#include "../flatbuf/emc_error_generated.h"
#include "../flatbuf/emc_stat_generated.h"
#include "emc.hh"

#include <flatbuffers/flatbuffers.h>
#include <iostream>
#include <zmq.hpp>

static zmq::context_t context;

ZMQCom::ZMQCom() = default;

ZMQCom::~ZMQCom() = default;

void ZMQCom::init()
{
  // set up 0mq sockets
  // this should probably be configurable,
  // at least with something like LINUXCNC_INSTANCE
  command_socket =
      std::make_unique<zmq::socket_t>(context, zmq::socket_type::push);
  command_socket->connect("ipc://@/tmp/linuxcnc-command");
  // command_socket->connect("tcp://127.0.0.1:5027");

  status_socket =
      std::make_unique<zmq::socket_t>(context, zmq::socket_type::sub);
  status_socket->connect("ipc://@/tmp/linuxcnc-status");
  // status_socket->connect("tcp://127.0.0.1:5028");
  status_socket->set(zmq::sockopt::subscribe, "");
  status_socket->set(zmq::sockopt::rcvhwm, 100);
  // only keep last status update
  // att, conflate is not compatible with subscription filters
  //status_socket->set(zmq::sockopt::conflate, 1);
  m_status = std::make_unique<EMC::EmcStatT>();

  error_socket =
      std::make_unique<zmq::socket_t>(context, zmq::socket_type::sub);
  error_socket->connect("ipc://@/tmp/linuxcnc-error");
  // error_socket->connect("tcp://127.0.0.1:5029");
}

int ZMQCom::update_status()
{
  std::string s;
  zmq::message_t msg, msg2;
  auto res = status_socket->recv(msg2, zmq::recv_flags::dontwait);
  if (res.has_value()) {
    int i = 0;
    while (res.has_value()) {
      msg.swap(msg2);
      i++;
      res = status_socket->recv(msg2, zmq::recv_flags::dontwait);
    }

    //std::cerr << "received status " << i << std::endl;
    auto status = EMC::GetEmcStat(msg.data());
    m_status = std::unique_ptr<EMC::EmcStatT>(status->UnPack());
  }
  return 0;
}

EMC::EmcStatT& ZMQCom::status() { return *m_status.get(); }

const EMC::EmcStatT& ZMQCom::status() const { return *m_status.get(); }

int ZMQCom::update_error() { return 0; }

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

int ZMQCom::_send_task_set_state(EMC_TASK_STATE state)
{
  return _send_command<EMC::TaskSetState>(static_cast<int>(state));
}

int ZMQCom::_send_task_set_mode(EMC_TASK_MODE mode)
{
  return _send_command<EMC::TaskSetMode>(static_cast<int>(mode));
}

template <typename s>
int ZMQCom::_send_simple_command()
{
  return _send_command<s>();
}

template <typename s, class... Args>
int ZMQCom::_send_command(Args&&... args)
{
  flatbuffers::FlatBufferBuilder fbb;
  auto cmd = fbb.CreateStruct(s(std::forward<Args>(args)...));
  auto msg = EMC::CmdChannelMsgBuilder(fbb);
  msg.add_command(cmd.Union());
  msg.add_command_type(EMC::Command_task_set_state);
  fbb.Finish(msg.Finish());
  return emc_command_send(fbb);
}

int ZMQCom::send_debug(int level)
{
  return _send_command<EMC::SetDebug>(level);
}

int ZMQCom::send_ESTOP() { return _send_task_set_state(EMC_TASK_STATE::ESTOP); }

int ZMQCom::send_ESTOP_reset()
{
  return _send_task_set_state(EMC_TASK_STATE::ESTOP_RESET);
}

int ZMQCom::send_machine_on()
{
  return _send_task_set_state(EMC_TASK_STATE::ON);
}

int ZMQCom::send_machine_off()
{
  return _send_task_set_state(EMC_TASK_STATE::OFF);
}

int ZMQCom::send_manual() { return _send_task_set_mode(EMC_TASK_MODE::MANUAL); }

int ZMQCom::send_auto() { return _send_task_set_mode(EMC_TASK_MODE::AUTO); }

int ZMQCom::send_mdi() { return _send_task_set_mode(EMC_TASK_MODE::MDI); }

int ZMQCom::send_override_limits(int joint)
{
  return _send_command<EMC::JointOverrideLimits>(joint);
}

int ZMQCom::send_jog_stop(int ja, int jjogmode)
{
  return _send_command<EMC::JogStop>(ja, jjogmode);
}

int ZMQCom::send_jog_cont(int ja, int jjogmode, double speed)
{
  return _send_command<EMC::JogCont>(ja, jjogmode, speed);
}

int ZMQCom::send_jog_incr(int ja, int jjogmode, double speed, double incr)
{
  return _send_command<EMC::JogIncr>(ja, jjogmode, speed, incr);
}

int ZMQCom::send_mist_on() { return _send_command<EMC::CoolantMistOn>(); }

int ZMQCom::send_mist_off() { return _send_command<EMC::CoolantMistOff>(); }

int ZMQCom::send_flood_on() { return _send_command<EMC::CoolantFloodOn>(); }

int ZMQCom::send_flood_off() { return _send_command<EMC::CoolantFloodOff>(); }

int ZMQCom::send_lube_on() { return -1; }
int ZMQCom::send_lube_off() { return -1; }

int ZMQCom::send_spindle_forward(int spindle)
{
  if (m_status)
    return _send_command<EMC::SpindleOn>(
        spindle, m_status->task->active_settings[2], 0, 0, 0);
  else
    return _send_command<EMC::SpindleOn>(spindle, 500, 0, 0, 0);
}

int ZMQCom::send_spindle_reverse(int spindle)
{
  if (m_status)
    return _send_command<EMC::SpindleOn>(
        spindle, -1.0 * m_status->task->active_settings[2], 0, 0, 0);
  else
    return _send_command<EMC::SpindleOn>(spindle, -500, 0, 0, 0);
}

int ZMQCom::send_spindle_off(int spindle)
{
  return _send_command<EMC::SpindleOff>(spindle);
}

int ZMQCom::send_spindle_increase(int spindle)
{
  return _send_command<EMC::SpindleIncrease>(spindle, 0);
}

int ZMQCom::send_spindle_decrease(int spindle)
{
  return _send_command<EMC::SpindleIncrease>(spindle, 0);
}

int ZMQCom::send_spindle_constant(int spindle)
{
  return _send_command<EMC::SpindleConstant>(spindle, 0);
}

int ZMQCom::send_spindle_brake_engage(int spindle)
{
  return _send_command<EMC::SpindleBrakeEngage>(spindle);
}

int ZMQCom::send_spindle_brake_release(int spindle)
{
  return _send_command<EMC::SpindleBrakeRelease>(spindle);
}

int ZMQCom::send_abort() { return _send_command<EMC::TrajAbort>(); }

int ZMQCom::send_home(int joint)
{
  return _send_command<EMC::JointHome>(joint);
}

int ZMQCom::send_un_home(int joint)
{
  return _send_command<EMC::JointUnhome>(joint);
}

int ZMQCom::send_feed_override(double override)
{
  return _send_command<EMC::TrajSetScale>(override);
}
int ZMQCom::send_rapid_override(double override) { return -1; }
int ZMQCom::send_spindle_override(int spindle, double override) { return -1; }

int ZMQCom::send_task_plan_init() { return _send_command<EMC::TaskPlanInit>(); }

int ZMQCom::send_program_open(char* program) { return -1; }
int ZMQCom::send_program_run(int line) { return -1; }
int ZMQCom::send_program_pause() { return -1; }

int ZMQCom::send_program_resume()
{
  return _send_command<EMC::TaskPlanResume>();
}

int ZMQCom::send_set_optional_stop(bool state)
{
  return _send_command<EMC::TaskPlanSetOptionalStop>(state);
}

int ZMQCom::send_program_step() { return _send_command<EMC::TaskPlanStep>(); }

int ZMQCom::send_mdi_cmd(const char* mdi) { return -1; }

int ZMQCom::send_load_tool_table(const char* file) { return -1; }

int ZMQCom::send_tool_set_offset(int toolno, double zoffset, double diameter)
{
  return -1;
}

int ZMQCom::send_tool_set_offset(int toolno, double zoffset, double xoffset,
                                 double diameter, double frontangle,
                                 double backangle, int orientation)
{
  return -1;
}

int ZMQCom::send_joint_set_backlash(int joint, double backlash)
{
  return _send_command<EMC::JointSetBacklash>(joint, backlash);
}

int ZMQCom::send_joint_enable(int joint, int val) { return 0; }

int ZMQCom::send_joint_load_comp(int joint, const char* file, int type)
{
  return -1;
}

int ZMQCom::send_set_teleop_enable(int enable)
{
  return _send_command<EMC::TrajSetTeleopEnable>(enable);
}

int ZMQCom::send_clear_probe_tripped_flag()
{
  return _send_command<EMC::TrajClearProbeTrippedFlag>();
}

int ZMQCom::send_probe(double x, double y, double z)
{
  EMC::Pose pose(x, y, z, 0, 0, 0, 0, 0, 0);
  return _send_command<EMC::TrajProbe>(pose, 0, 0, 0, 0, 0);
}

int ZMQCom::ini_load(const char* filename) { return -1; }
