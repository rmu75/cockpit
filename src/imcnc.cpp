/*
 * imcnc.cpp
 *
 * (c) 2022-2023 Robert Schöftner rs@unfoo.net
 */

// clang-format off
#include <signal.h>

#include "imgui.h"
// clang-format on

#define TOOL_NML

#include "rcs.hh"
#include "posemath.h" // PM_POSE, TO_RAD
#include "emc.hh"     // EMC NML
#include "canon.hh"   // CANON_UNITS, CANON_UNITS_INCHES,MM,CM
#include "emcglb.h"   // EMC_NMLFILE, TRAJ_MAX_VELOCITY, etc.
#include "emccfg.h"   // DEFAULT_TRAJ_MAX_VELOCITY
#include "inifile.hh" // INIFILE
#include "config.h"   // Standard path definitions
#include "rcs_print.hh"
#include "shcom.hh"

namespace ImCNC {

int quitting = 0;

static void sigQuit(int sig)
{
  quitting = 1;
}

int init(int argc, char* argv[])
{
  // process command line args
  if (emcGetArgs(argc, argv) != 0) {
    rcs_print_error("error in argument list\n");
    exit(1);
  }
  // get configuration information
  iniLoad(emc_inifile);
  // init NML
  if (tryNml() != 0) {
    rcs_print_error("can't connect to emc\n");
    // thisQuit();
    exit(1);
  }
  // get current serial number, and save it for restoring when we quit
  // so as not to interfere with real operator interface
  updateStatus();
  emcCommandSerialNumber = emcStatus->echo_serial_number;

  // attach our quit function to SIGINT
  signal(SIGTERM, sigQuit);

  return 0;
}

//-----------------------------------------------------------------------------
// [SECTION] Example App: Debug Log / ShowExampleAppLog()
//-----------------------------------------------------------------------------

// Usage:
//  static ExampleAppLog my_log;
//  my_log.AddLog("Hello %d world\n", 123);
//  my_log.Draw("title");
struct LogWindow
{
  ImGuiTextBuffer Buf;
  ImGuiTextFilter Filter;
  ImVector<int> LineOffsets; // Index to lines offset. We maintain this with
                             // AddLog() calls.
  bool AutoScroll;           // Keep scrolling if already at the bottom.

  LogWindow()
  {
    AutoScroll = true;
    Clear();
  }

  void Clear()
  {
    Buf.clear();
    LineOffsets.clear();
    LineOffsets.push_back(0);
  }

  void AddLog(const char* fmt, ...) IM_FMTARGS(2)
  {
    int old_size = Buf.size();
    va_list args;
    va_start(args, fmt);
    Buf.appendfv(fmt, args);
    va_end(args);
    for (int new_size = Buf.size(); old_size < new_size; old_size++)
      if (Buf[old_size] == '\n')
        LineOffsets.push_back(old_size + 1);
  }

  void Draw(const char* title, bool* p_open = NULL)
  {
    if (!ImGui::Begin(title, p_open)) {
      ImGui::End();
      return;
    }

    // Options menu
    if (ImGui::BeginPopup("Options")) {
      ImGui::Checkbox("Auto-scroll", &AutoScroll);
      ImGui::EndPopup();
    }

    // Main window
    if (ImGui::Button("Options"))
      ImGui::OpenPopup("Options");
    ImGui::SameLine();
    bool clear = ImGui::Button("Clear");
    ImGui::SameLine();
    bool copy = ImGui::Button("Copy");
    ImGui::SameLine();
    Filter.Draw("Filter", -100.0f);

    ImGui::Separator();
    ImGui::BeginChild("scrolling", ImVec2(0, 0), false,
                      ImGuiWindowFlags_HorizontalScrollbar);

    if (clear)
      Clear();
    if (copy)
      ImGui::LogToClipboard();

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    const char* buf = Buf.begin();
    const char* buf_end = Buf.end();
    if (Filter.IsActive()) {
      // In this example we don't use the clipper when Filter is enabled.
      // This is because we don't have a random access on the result on our
      // filter. A real application processing logs with ten of thousands of
      // entries may want to store the result of search/filter.. especially if
      // the filtering function is not trivial (e.g. reg-exp).
      for (int line_no = 0; line_no < LineOffsets.Size; line_no++) {
        const char* line_start = buf + LineOffsets[line_no];
        const char* line_end = (line_no + 1 < LineOffsets.Size)
                                   ? (buf + LineOffsets[line_no + 1] - 1)
                                   : buf_end;
        if (Filter.PassFilter(line_start, line_end))
          ImGui::TextUnformatted(line_start, line_end);
      }
    }
    else {
      // The simplest and easy way to display the entire buffer:
      //   ImGui::TextUnformatted(buf_begin, buf_end);
      // And it'll just work. TextUnformatted() has specialization for large
      // blob of text and will fast-forward to skip non-visible lines. Here we
      // instead demonstrate using the clipper to only process lines that are
      // within the visible area.
      // If you have tens of thousands of items and their processing cost is
      // non-negligible, coarse clipping them on your side is recommended. Using
      // ImGuiListClipper requires
      // - A) random access into your data
      // - B) items all being the  same height,
      // both of which we can handle since we an array pointing to the beginning
      // of each line of text. When using the filter (in the block of code
      // above) we don't have random access into the data to display anymore,
      // which is why we don't use the clipper. Storing or skimming through the
      // search result would make it possible (and would be recommended if you
      // want to search through tens of thousands of entries).
      ImGuiListClipper clipper;
      clipper.Begin(LineOffsets.Size);
      while (clipper.Step()) {
        for (int line_no = clipper.DisplayStart; line_no < clipper.DisplayEnd;
             line_no++)
        {
          const char* line_start = buf + LineOffsets[line_no];
          const char* line_end = (line_no + 1 < LineOffsets.Size)
                                     ? (buf + LineOffsets[line_no + 1] - 1)
                                     : buf_end;
          ImGui::TextUnformatted(line_start, line_end);
        }
      }
      clipper.End();
    }
    ImGui::PopStyleVar();

    if (AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
      ImGui::SetScrollHereY(1.0f);

    ImGui::EndChild();
    ImGui::End();
  }
};

// Demonstrate creating a simple log window with basic filtering.
static void ShowLogWindow(bool* p_open)
{
  static LogWindow log;
  auto res = updateError();

  if (res == 0) {
    if (*error_string)
      log.AddLog("%s\n", error_string);
    if (*operator_text_string)
      log.AddLog("%s\n", operator_text_string);
    *error_string = 0;
    *operator_text_string = 0;
  }

  // For the demo: add a debug button _BEFORE_ the normal log window contents
  // We take advantage of a rarely used feature: multiple calls to Begin()/End()
  // are appending to the _same_ window. Most of the contents of the window will
  // be added by the log.Draw() call.
  ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
  // ImGui::Begin("Log", p_open);
  // if (ImGui::SmallButton("clear")) {
  //   log.Clear();
  // }
  // ImGui::End();

  // Actually call in the regular Log helper (which will Begin() into the same
  // window as we just did)
  log.Draw("Log", p_open);
}

void ShowWindow()
{
  updateStatus();
  ImGuiIO& io = ImGui::GetIO();

  if (ImGui::Begin("Traj Status")) {
    ImGui::Text("linear units: %f", emcStatus->motion.traj.linearUnits);
    ImGui::Text("angular units: %f", emcStatus->motion.traj.angularUnits);
    ImGui::Text("cycle time: %f", emcStatus->motion.traj.cycleTime);
    ImGui::Text("joints: %d", emcStatus->motion.traj.joints);
    ImGui::Text("scale: %f", emcStatus->motion.traj.scale);
    ImGui::Text("rapid scale: %f", emcStatus->motion.traj.rapid_scale);
    ImGui::Text("spindles: %d", emcStatus->motion.traj.spindles);
    ImGui::Text("acceleration: %9.3f", emcStatus->motion.traj.acceleration);
    ImGui::Text("velocity: %9.3f", emcStatus->motion.traj.velocity);
    ImGui::Text("distance to go: %9.3f", emcStatus->motion.traj.distance_to_go);
    ImGui::Text("current velocity: %9.3f", emcStatus->motion.traj.current_vel);
    ImGui::Text("queue: %d activeQueue: %d full: %d id: %d",
                emcStatus->motion.traj.queue,
                emcStatus->motion.traj.activeQueue,
                emcStatus->motion.traj.queueFull, emcStatus->motion.traj.id);
    ImGui::Text("motion type %d", emcStatus->motion.traj.motion_type);
    if (emcStatus->motion.traj.feed_override_enabled)
      ImGui::Text("feed override");
    if (emcStatus->motion.traj.adaptive_feed_enabled)
      ImGui::Text("adaptive feed");
    if (emcStatus->motion.traj.feed_hold_enabled)
      ImGui::Text("feed hold");
  }
  ImGui::End();

  if (ImGui::Begin("Position")) {
    if (ImGui::BeginTable("##position_table", 4,
                          ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_BordersInnerV))
    {
      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
      ImGui::TableSetupColumn("CMD");
      ImGui::TableSetupColumn("ACT");
      ImGui::TableSetupColumn("DTG");
      ImGui::TableHeadersRow();

      ImGui::PushFont(io.Fonts->Fonts[3]);

      ImVec4 color1 = ImColor::HSV(1 / 7.f, 0.6f, 0.6f);
      ImVec4 color2 = ImColor::HSV(2 / 7.f, 0.6f, 0.6f);
      ImVec4 color3 = ImColor::HSV(0 / 7.f, 0.6f, 0.6f);

      const auto& traj = emcStatus->motion.traj;
      struct
      {
        const bool active;
        const char* label;
        const double cmd, act, dtg;
      } axis_values[] = {{(traj.axis_mask & 1) != 0, "X", traj.position.tran.x,
                          traj.actualPosition.tran.x, traj.dtg.tran.x},
                         {(traj.axis_mask & 2) != 0, "Y", traj.position.tran.y,
                          traj.actualPosition.tran.y, traj.dtg.tran.y},
                         {(traj.axis_mask & 4) != 0, "Z", traj.position.tran.z,
                          traj.actualPosition.tran.z, traj.dtg.tran.z},
                         {(traj.axis_mask & 8) != 0, "A", traj.position.a,
                          traj.actualPosition.a, traj.dtg.a},
                         {(traj.axis_mask & 16) != 0, "B", traj.position.b,
                          traj.actualPosition.b, traj.dtg.b},
                         {(traj.axis_mask & 32) != 0, "C", traj.position.c,
                          traj.actualPosition.c, traj.dtg.c},
                         {(traj.axis_mask & 64) != 0, "U", traj.position.u,
                          traj.actualPosition.u, traj.dtg.u},
                         {(traj.axis_mask & 128) != 0, "V", traj.position.v,
                          traj.actualPosition.v, traj.dtg.v},
                         {(traj.axis_mask & 256) != 0, "W", traj.position.w,
                          traj.actualPosition.w, traj.dtg.w}};

      for (const auto& axis : axis_values) {
        if (axis.active) {
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          ImGui::TextUnformatted(axis.label);
          ImGui::TableNextColumn();
          ImGui::PushStyleColor(ImGuiCol_Text, color1);
          ImGui::Text("%9.3f", axis.cmd);
          ImGui::TableNextColumn();
          ImGui::PushStyleColor(ImGuiCol_Text, color2);
          ImGui::Text("%9.3f", axis.act);
          ImGui::PushStyleColor(ImGuiCol_Text, color3);
          ImGui::TableNextColumn();
          ImGui::Text("%9.3f", axis.dtg);
          ImGui::PopStyleColor(3);
        }
      }

      ImGui::PopFont();
      ImGui::EndTable();
    }
  }
  ImGui::End();

  if (ImGui::Begin("Probe")) {
    ImGui::PushFont(io.Fonts->Fonts[1]);
    if (emcStatus->motion.traj.axis_mask & 1)
      ImGui::Text("X %9.3f", emcStatus->motion.traj.probedPosition.tran.x);
    if (emcStatus->motion.traj.axis_mask & 2)
      ImGui::Text("Y %9.3f", emcStatus->motion.traj.probedPosition.tran.y);
    if (emcStatus->motion.traj.axis_mask & 4)
      ImGui::Text("Z %9.3f", emcStatus->motion.traj.probedPosition.tran.z);
    if (emcStatus->motion.traj.axis_mask & 8)
      ImGui::Text("A %9.3f", emcStatus->motion.traj.probedPosition.a);
    if (emcStatus->motion.traj.axis_mask & 16)
      ImGui::Text("B %9.3f", emcStatus->motion.traj.probedPosition.b);
    if (emcStatus->motion.traj.axis_mask & 32)
      ImGui::Text("C %9.3f", emcStatus->motion.traj.probedPosition.c);
    if (emcStatus->motion.traj.axis_mask & 64)
      ImGui::Text("U %9.3f", emcStatus->motion.traj.probedPosition.u);
    if (emcStatus->motion.traj.axis_mask & 128)
      ImGui::Text("V %9.3f", emcStatus->motion.traj.probedPosition.v);
    if (emcStatus->motion.traj.axis_mask & 256)
      ImGui::Text("W %9.3f", emcStatus->motion.traj.probedPosition.w);
    ImGui::Text("probe val: %d", emcStatus->motion.traj.probeval);
    if (emcStatus->motion.traj.probing)
      ImGui::Text("probing");
    if (emcStatus->motion.traj.probe_tripped)
      ImGui::Text("probe tripped");
    ImGui::PopFont();
  }
  ImGui::End();

  if (ImGui::Begin("External Offset")) {
    ImGui::PushFont(io.Fonts->Fonts[1]);
    if (emcStatus->motion.traj.axis_mask & 1)
      ImGui::Text("X %9.3f", emcStatus->motion.eoffset_pose.tran.x);
    if (emcStatus->motion.traj.axis_mask & 2)
      ImGui::Text("Y %9.3f", emcStatus->motion.eoffset_pose.tran.y);
    if (emcStatus->motion.traj.axis_mask & 4)
      ImGui::Text("Z %9.3f", emcStatus->motion.eoffset_pose.tran.z);
    if (emcStatus->motion.traj.axis_mask & 8)
      ImGui::Text("A %9.3f", emcStatus->motion.eoffset_pose.a);
    if (emcStatus->motion.traj.axis_mask & 16)
      ImGui::Text("B %9.3f", emcStatus->motion.eoffset_pose.b);
    if (emcStatus->motion.traj.axis_mask & 32)
      ImGui::Text("C %9.3f", emcStatus->motion.eoffset_pose.c);
    if (emcStatus->motion.traj.axis_mask & 64)
      ImGui::Text("U %9.3f", emcStatus->motion.eoffset_pose.u);
    if (emcStatus->motion.traj.axis_mask & 128)
      ImGui::Text("V %9.3f", emcStatus->motion.eoffset_pose.v);
    if (emcStatus->motion.traj.axis_mask & 256)
      ImGui::Text("W %9.3f", emcStatus->motion.eoffset_pose.w);
    if (emcStatus->motion.external_offsets_applied)
      ImGui::Text("external offsets applied");
    ImGui::PopFont();
  }
  ImGui::End();

  if (ImGui::Begin("Joints")) {
    ImGui::PushFont(io.Fonts->Fonts[3]);
    for (unsigned index = 0; index < emcStatus->motion.traj.joints; index++) {
      auto& joint = emcStatus->motion.joint[index];
      // ImGui::Text("%d %c %9.3f %9.3f", index, joint.homed ? '*' : ' ',
      //            joint.output, joint.input);
      if (ImGui::TreeNode(&joint, "%d %c %9.3f %9.3f", index,
                          joint.homed ? '*' : ' ', joint.output, joint.input))
      {
        ImGui::PushFont(io.Fonts->Fonts[0]);
        ImGui::Text("type %d", joint.jointType);
        ImGui::Text("units %f", joint.units);
        ImGui::Text("backlash %f", joint.backlash);
        ImGui::Text("limits %f %f", joint.minPositionLimit,
                    joint.maxPositionLimit);
        ImGui::Text("ferror %f %f", joint.maxFerror, joint.minFerror);
        ImGui::Text("ferrorCurrent %9.3f", joint.ferrorCurrent);
        ImGui::Text("ferrorHighMark %9.3f", joint.ferrorHighMark);
        ImGui::Text("velocity %9.3f", joint.velocity);
        if (joint.inpos)
          ImGui::Text("in position");
        if (joint.homing)
          ImGui::Text("homing");
        if (joint.fault)
          ImGui::Text("fault");
        if (joint.enabled)
          ImGui::Text("enabled");
        if (joint.minSoftLimit)
          ImGui::Text("min soft limit exceeded");
        if (joint.maxSoftLimit)
          ImGui::Text("max soft limit exceeded");
        if (joint.minHardLimit)
          ImGui::Text("min hard limit exceeded");
        if (joint.maxHardLimit)
          ImGui::Text("max hard limit exceeded");
        if (joint.overrideLimits)
          ImGui::Text("override limits");
        ImGui::TreePop();
        ImGui::PopFont();
      }
    }
    ImGui::PopFont();
  }
  ImGui::End();

  if (ImGui::Begin("Spindles")) {
    for (unsigned index = 0; index < emcStatus->motion.traj.spindles; index++) {
      auto& spindle = emcStatus->motion.spindle[index];
      const char* dir = "STOP";
      if (spindle.direction == 1)
        dir = "FORWARD (CW)";
      if (spindle.direction == -1)
        dir = "REVERSE (CCW)";
      if (ImGui::TreeNode(&spindle, "%d: %f %s", index, spindle.speed, dir)) {
        ImGui::Text("scale: %f", spindle.spindle_scale);
        ImGui::Text("css maximum: %f", spindle.css_maximum);
        ImGui::Text("css factor: %f", spindle.css_factor);
        if (spindle.enabled)
          ImGui::Text("enabled");
        if (spindle.homed)
          ImGui::Text("homed");
        if (spindle.brake)
          ImGui::Text("brake");
        if (spindle.increasing > 0)
          ImGui::Text("increasing");
        if (spindle.increasing < 0)
          ImGui::Text("decreasing");
        ImGui::Text("orient: %d %d", spindle.orient_state,
                    spindle.orient_fault);
        if (spindle.spindle_override_enabled)
          ImGui::Text("override enabled");
        ImGui::TreePop();
      }
    }
  }
  ImGui::End();

  if (ImGui::Begin("Tag")) {
    auto flags = emcStatus->motion.traj.tag.packed_flags;
    if (flags & (1 << GM_FLAG_UNITS))
      ImGui::Text("units");
    if (flags & (1 << GM_FLAG_DISTANCE_MODE))
      ImGui::Text("distance mode");
    if (flags & (1 << GM_FLAG_TOOL_OFFSETS_ON))
      ImGui::Text("tool offsets on");
    if (flags & (1 << GM_FLAG_RETRACT_OLDZ))
      ImGui::Text("retract old Z");
    if (flags & (1 << GM_FLAG_BLEND))
      ImGui::Text("blend");
    if (flags & (1 << GM_FLAG_EXACT_STOP))
      ImGui::Text("exact stop");
    if (flags & (1 << GM_FLAG_FEED_INVERSE_TIME))
      ImGui::Text("feed inverse time");
    if (flags & (1 << GM_FLAG_FEED_UPM))
      ImGui::Text("feed upm");
    if (flags & (1 << GM_FLAG_CSS_MODE))
      ImGui::Text("css mode");
    if (flags & (1 << GM_FLAG_IJK_ABS))
      ImGui::Text("IJK abs");
    if (flags & (1 << GM_FLAG_DIAMETER_MODE))
      ImGui::Text("diameter mode");
    if (flags & (1 << GM_FLAG_G92_IS_APPLIED))
      ImGui::Text("G92 applied");
    if (flags & (1 << GM_FLAG_SPINDLE_ON)) {
      ImGui::Text("SPINDLE ON");
      if (flags & (1 << GM_FLAG_SPINDLE_CW))
        ImGui::Text("FORWARD (CW)");
      else
        ImGui::Text("REVERSE (CCW)");
    }
    if (flags & (1 << GM_FLAG_MIST))
      ImGui::Text("mist");
    if (flags & (1 << GM_FLAG_FLOOD))
      ImGui::Text("flood");
    if (flags & (1 << GM_FLAG_FEED_OVERRIDE))
      ImGui::Text("feed override");
    if (flags & (1 << GM_FLAG_SPEED_OVERRIDE))
      ImGui::Text("speed override");
    if (flags & (1 << GM_FLAG_ADAPTIVE_FEED))
      ImGui::Text("adaptive feed");
    if (flags & (1 << GM_FLAG_FEED_HOLD))
      ImGui::Text("feed hold");
    if (flags & (1 << GM_FLAG_RESTORABLE))
      ImGui::Text("restorable");
    if (flags & (1 << GM_FLAG_IN_REMAP))
      ImGui::Text("in remap");
    if (flags & (1 << GM_FLAG_IN_SUB))
      ImGui::Text("in sub");
    if (flags & (1 << GM_FLAG_EXTERNAL_FILE))
      ImGui::Text("external file");

    // field indices are in StateField enum
    ImGui::Text("line nr: %d", emcStatus->motion.traj.tag.fields[0]);
    ImGui::Text("G mode 0: %d", emcStatus->motion.traj.tag.fields[1]);
    ImGui::Text("cutter comp on: %d", emcStatus->motion.traj.tag.fields[2]);
    ImGui::Text("motion mode: %d", emcStatus->motion.traj.tag.fields[3]);
    ImGui::Text("plane: %d", emcStatus->motion.traj.tag.fields[4]);
    ImGui::Text("M modes 4: %d", emcStatus->motion.traj.tag.fields[5]);
    ImGui::Text("origin: %d", emcStatus->motion.traj.tag.fields[6]);
    ImGui::Text("toolchange: %d", emcStatus->motion.traj.tag.fields[7]);
    // field indices are in StateFieldFloat enum
    ImGui::Text("line nr: %f", emcStatus->motion.traj.tag.fields_float[0]);
    ImGui::Text("feedrate: %f", emcStatus->motion.traj.tag.fields_float[1]);
    ImGui::Text("speed: %f", emcStatus->motion.traj.tag.fields_float[2]);
    ImGui::Text("path tolerance: %f",
                emcStatus->motion.traj.tag.fields_float[3]);
    ImGui::Text("naive CAM tolerance: %f",
                emcStatus->motion.traj.tag.fields_float[4]);
    ImGui::Text("filename %s", emcStatus->motion.traj.tag.filename);
  }
  ImGui::End();

  if (ImGui::Begin("Task")) {
    const char* mode = "invalid";
    const char* state = "invalid";
    const char* execState = "invalid";
    const char* interpState = "invalid";

    switch (emcStatus->task.mode) {
    case EMC_TASK_MODE_ENUM::EMC_TASK_MODE_AUTO:
      mode = "AUTO";
      break;
    case EMC_TASK_MODE_ENUM::EMC_TASK_MODE_MANUAL:
      mode = "MANUAL";
      break;
    case EMC_TASK_MODE_ENUM::EMC_TASK_MODE_MDI:
      mode = "MDI";
      break;
    }

    switch (emcStatus->task.state) {
    case EMC_TASK_STATE_ENUM::EMC_TASK_STATE_ESTOP:
      state = "ESTOP";
      break;
    case EMC_TASK_STATE_ENUM::EMC_TASK_STATE_ESTOP_RESET:
      state = "ESTOP_RESET";
      break;
    case EMC_TASK_STATE_ENUM::EMC_TASK_STATE_OFF:
      state = "OFF";
      break;
    case EMC_TASK_STATE_ENUM::EMC_TASK_STATE_ON:
      state = "ON";
      break;
    }

    switch (emcStatus->task.execState) {
    case EMC_TASK_EXEC_DONE:
      execState = "done";
      break;
    case EMC_TASK_EXEC_ERROR:
      execState = "error";
      break;
    case EMC_TASK_EXEC_WAITING_FOR_IO:
      execState = "waiting for I/O";
      break;
    case EMC_TASK_EXEC_WAITING_FOR_DELAY:
      execState = "waiting for delay";
      break;
    case EMC_TASK_EXEC_WAITING_FOR_MOTION:
      execState = "waiting for motion";
      break;
    case EMC_TASK_EXEC_WAITING_FOR_SYSTEM_CMD:
      execState = "waiting for system command";
      break;
    case EMC_TASK_EXEC_WAITING_FOR_MOTION_QUEUE:
      execState = "waiting for motion queue";
      break;
    case EMC_TASK_EXEC_WAITING_FOR_MOTION_AND_IO:
      execState = "waiting for motion and I/O";
      break;
    case EMC_TASK_EXEC_WAITING_FOR_SPINDLE_ORIENTED:
      execState = "waiting for motion and spindle oriented";
      break;
    }

    switch (emcStatus->task.interpState) {
    case EMC_TASK_INTERP_IDLE:
      interpState = "idle";
      break;
    case EMC_TASK_INTERP_PAUSED:
      interpState = "paused";
      break;
    case EMC_TASK_INTERP_READING:
      interpState = "reading";
      break;
    case EMC_TASK_INTERP_WAITING:
      interpState = "waiting";
      break;
    }

    {
      std::ostringstream buf;
      auto it = std::begin(emcStatus->task.activeGCodes);
      // buf << "Line " << *it << ' ';
      ++it;

      for (auto end = std::end(emcStatus->task.activeGCodes); it != end; ++it) {
        auto code = *it;
        if (code == -1)
          continue;
        if (code % 10) {
          buf << 'G' << code / 10 << '.' << code % 10 << ' ';
        }
        else {
          buf << 'G' << code / 10 << ' ';
        }
      }
      ImGui::TextUnformatted(buf.str().c_str());
    }

    {
      std::ostringstream buf;
      auto it = std::begin(emcStatus->task.activeMCodes);
      ++it; // skip line nr

      for (auto end = std::end(emcStatus->task.activeMCodes); it != end; ++it) {
        auto code = *it;
        if (code == -1)
          continue;
        buf << 'M' << code << ' ';
      }
      ImGui::TextUnformatted(buf.str().c_str());
    }

    ImGui::Text("F%.0f S%.0f", emcStatus->task.activeSettings[1],
                emcStatus->task.activeSettings[2]);

    ImGui::Text("Mode %s State %s execState %s interpState %s", mode, state,
                execState, interpState);
    ImGui::Text("callLevel %d motionLine %d currentLine %d readLine %d",
                emcStatus->task.callLevel, emcStatus->task.motionLine,
                emcStatus->task.currentLine, emcStatus->task.readLine);
    ImGui::Text("File %s", emcStatus->task.file);
    ImGui::Text("Command %s", emcStatus->task.command);
    if (ImGui::TreeNode("Offsets", "Offsets %d/G92/tool rot %f",
                        emcStatus->task.g5x_index, emcStatus->task.rotation_xy))
    {
      ImGui::PushFont(io.Fonts->Fonts[3]);
      if (emcStatus->motion.traj.axis_mask & 1)
        ImGui::Text("X %9.3f %9.3f %9.3f", emcStatus->task.g5x_offset.tran.x,
                    emcStatus->task.g92_offset.tran.x,
                    emcStatus->task.toolOffset.tran.x);
      if (emcStatus->motion.traj.axis_mask & 2)
        ImGui::Text("Y %9.3f %9.3f %9.3f", emcStatus->task.g5x_offset.tran.y,
                    emcStatus->task.g92_offset.tran.y,
                    emcStatus->task.toolOffset.tran.y);
      if (emcStatus->motion.traj.axis_mask & 4)
        ImGui::Text("Z %9.3f %9.3f %9.3f", emcStatus->task.g5x_offset.tran.z,
                    emcStatus->task.g92_offset.tran.z,
                    emcStatus->task.toolOffset.tran.z);
      if (emcStatus->motion.traj.axis_mask & 8)
        ImGui::Text("A %9.3f %9.3f %9.3f", emcStatus->task.g5x_offset.a,
                    emcStatus->task.g92_offset.a, emcStatus->task.toolOffset.a);
      if (emcStatus->motion.traj.axis_mask & 16)
        ImGui::Text("B %9.3f %9.3f %9.3f", emcStatus->task.g5x_offset.b,
                    emcStatus->task.g92_offset.b, emcStatus->task.toolOffset.b);
      if (emcStatus->motion.traj.axis_mask & 32)
        ImGui::Text("C %9.3f %9.3f %9.3f", emcStatus->task.g5x_offset.c,
                    emcStatus->task.g92_offset.c, emcStatus->task.toolOffset.c);
      if (emcStatus->motion.traj.axis_mask & 64)
        ImGui::Text("U %9.3f %9.3f %9.3f", emcStatus->task.g5x_offset.u,
                    emcStatus->task.g92_offset.u, emcStatus->task.toolOffset.u);
      if (emcStatus->motion.traj.axis_mask & 128)
        ImGui::Text("V %9.3f %9.3f %9.3f", emcStatus->task.g5x_offset.v,
                    emcStatus->task.g92_offset.v, emcStatus->task.toolOffset.v);
      if (emcStatus->motion.traj.axis_mask & 256)
        ImGui::Text("W %9.3f %9.3f %9.3f", emcStatus->task.g5x_offset.w,
                    emcStatus->task.g92_offset.w, emcStatus->task.toolOffset.w);
      ImGui::PopFont();
      ImGui::TreePop();
    }
  }
  ImGui::End();

  if (ImGui::Begin("Tools")) {
    ImGui::Text("pocket prepped: %d", emcStatus->io.tool.pocketPrepped);
    ImGui::Text("tool in spindle: %d", emcStatus->io.tool.toolInSpindle);

    if (ImGui::BeginTable("Tools", 4)) {
      ImGui::TableSetupColumn("Tool#", ImGuiTableColumnFlags_NoHide);
      ImGui::TableSetupColumn("Pocket#", ImGuiTableColumnFlags_NoHide);
      ImGui::TableSetupColumn("ø", ImGuiTableColumnFlags_NoHide);
      ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_NoHide);
      ImGui::TableHeadersRow();

      for (auto& tool : emcStatus->io.tool.toolTable) {
        if (tool.toolno < 0)
          continue;
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("%d", tool.toolno);
        ImGui::TableNextColumn();
        ImGui::Text("%d", tool.pocketno);
        ImGui::TableNextColumn();
        ImGui::Text("%f", tool.diameter);
      }
      ImGui::EndTable();
    }
  }
  ImGui::End();

  if (ImGui::Begin("CNC")) {
    ImGui::Text("test");
    ImGui::Text("state %d status %d", emcStatus->state, emcStatus->status);
    ImGui::Text("interpState %d", emcStatus->task.interpState);

    ImGui::Text("X %9.3f %9.3f", emcStatus->motion.joint[0].output,
                emcStatus->motion.joint[0].velocity);
    ImGui::Text("Y %9.3f %9.3f", emcStatus->motion.joint[1].output,
                emcStatus->motion.joint[1].velocity);
    ImGui::Text("Z %9.3f %9.3f", emcStatus->motion.joint[2].output,
                emcStatus->motion.joint[2].velocity);

    ImGui::Text("DTG: %f", emcStatus->motion.traj.distance_to_go);
    ImGui::Text("X %9.3f Y %9.3f Z %9.3f",
                emcStatus->motion.traj.actualPosition.tran.x,
                emcStatus->motion.traj.actualPosition.tran.y,
                emcStatus->motion.traj.actualPosition.tran.z);

    ImGui::Text("%d %s:%d", emcStatus->motion.traj.line,
                emcStatus->motion.traj.source_file,
                emcStatus->motion.traj.source_line);

    ImGui::Text("task.execState %d", emcStatus->task.execState);
    ImGui::Text("line %d interpState %d", emcStatus->task.motionLine,
                emcStatus->task.interpState);

    ImGui::Text("%s:%d", emcStatus->task.file, emcStatus->task.currentLine);
  }
  ImGui::End();

  if (ImGui::Begin("Commands")) {
    if (ImGui::Button("Abort")) {
      EMC_TASK_ABORT msg;
      emcCommandSend(msg);
    }
    if (ImGui::Button("ESTOP")) {
      EMC_TASK_SET_STATE msg;
      msg.state = EMC_TASK_STATE_ESTOP;
      emcCommandSend(msg);
    }
    if (ImGui::Button("ESTOP Reset")) {
      EMC_TASK_SET_STATE msg;
      msg.state = EMC_TASK_STATE_ESTOP_RESET;
      emcCommandSend(msg);
    }
    if (ImGui::Button("ON")) {
      EMC_TASK_SET_STATE msg;
      msg.state = EMC_TASK_STATE_ON;
      emcCommandSend(msg);
    }
    if (ImGui::Button("OFF")) {
      EMC_TASK_SET_STATE msg;
      msg.state = EMC_TASK_STATE_OFF;
      emcCommandSend(msg);
    }
    if (ImGui::Button("Manual")) {
      EMC_TASK_SET_MODE msg;
      msg.mode = EMC_TASK_MODE_MANUAL;
      emcCommandSend(msg);
    }
    if (ImGui::Button("Auto")) {
      EMC_TASK_SET_MODE msg;
      msg.mode = EMC_TASK_MODE_AUTO;
      emcCommandSend(msg);
    }
    if (ImGui::Button("MDI")) {
      EMC_TASK_SET_MODE msg;
      msg.mode = EMC_TASK_MODE_MDI;
      emcCommandSend(msg);
    }
  }
  ImGui::End();

  // if (ImGui::Begin("VTK")) {

  // }
  // ImGui::End();

  bool show_log_window = true;
  ShowLogWindow(&show_log_window);
}

void RightJustifiedText(const char* text)
{
  auto posX = (ImGui::GetCursorPosX() + ImGui::GetColumnWidth() -
               ImGui::CalcTextSize(text).x - ImGui::GetScrollX() -
               2 * ImGui::GetStyle().ItemSpacing.x);
  if (posX > ImGui::GetCursorPosX())
    ImGui::SetCursorPosX(posX);
  ImGui::Text("%s", text);
}

void ShowStatusWindow()
{
  updateStatus();
  ImGuiIO& io = ImGui::GetIO();

  constexpr char format_metric[] = "%9.3f";
  constexpr char format_imperial[] = "%8.4f";

  bool position_display_metric = true;
  bool position_display_actual = true;

  if (ImGui::Begin("Status Window")) {
    ImGui::BeginChild("ch1", ImVec2(ImGui::GetContentRegionAvail().x * 0.60f, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
    if (ImGui::BeginTable("##position_table", 3,
                          ImGuiTableFlags_RowBg))
    {
      ImGui::TableSetupColumn("WCS", ImGuiTableColumnFlags_WidthFixed);
      if (position_display_metric) {
        ImGui::TableSetupColumn("Position [mm]");
        ImGui::TableSetupColumn("Dist-to-go [mm]");
      }
      else {
        ImGui::TableSetupColumn("Position [in]");
        ImGui::TableSetupColumn("Dist-to-go [in]");
      }
      ImGui::TableHeadersRow();

      ImGui::PushFont(io.Fonts->Fonts[4]);

      static const ImVec4 color1 = ImColor::HSV(1 / 7.f, 0.6f, 0.6f);
      static const ImVec4 color2 = ImColor::HSV(2 / 7.f, 0.6f, 0.6f);
      static const ImVec4 color3 = ImColor::HSV(0 / 7.f, 0.6f, 0.6f);

      const auto& traj = emcStatus->motion.traj;
      struct
      {
        const bool active;
        const char* label;
        const double cmd, act, dtg;
      } axis_values[] = {{(traj.axis_mask & 1) != 0, "X", traj.position.tran.x,
                          traj.actualPosition.tran.x, traj.dtg.tran.x},
                         {(traj.axis_mask & 2) != 0, "Y", traj.position.tran.y,
                          traj.actualPosition.tran.y, traj.dtg.tran.y},
                         {(traj.axis_mask & 4) != 0, "Z", traj.position.tran.z,
                          traj.actualPosition.tran.z, traj.dtg.tran.z},
                         {(traj.axis_mask & 8) != 0, "A", traj.position.a,
                          traj.actualPosition.a, traj.dtg.a},
                         {(traj.axis_mask & 16) != 0, "B", traj.position.b,
                          traj.actualPosition.b, traj.dtg.b},
                         {(traj.axis_mask & 32) != 0, "C", traj.position.c,
                          traj.actualPosition.c, traj.dtg.c},
                         {(traj.axis_mask & 64) != 0, "U", traj.position.u,
                          traj.actualPosition.u, traj.dtg.u},
                         {(traj.axis_mask & 128) != 0, "V", traj.position.v,
                          traj.actualPosition.v, traj.dtg.v},
                         {(traj.axis_mask & 256) != 0, "W", traj.position.w,
                          traj.actualPosition.w, traj.dtg.w}};

      for (const auto& axis : axis_values) {
        if (axis.active) {
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          ImGui::TextUnformatted(axis.label);
          ImGui::TableNextColumn();
          if (position_display_metric) {
            char buf[16];
            snprintf(buf, sizeof(buf), format_metric,
                     position_display_actual ? axis.act : axis.cmd);
            RightJustifiedText(buf);
          }
          else {
            char buf[16];
            snprintf(buf, sizeof(buf), format_imperial,
                     (position_display_actual ? axis.act : axis.cmd) *
                         INCH_PER_MM);
            RightJustifiedText(buf);
          }
          ImGui::PushStyleColor(ImGuiCol_Text, color3);
          ImGui::TableNextColumn();
          if (position_display_metric) {
            char buf[16];
            snprintf(buf, sizeof(buf), format_metric, axis.dtg);
            RightJustifiedText(buf);
          }
          else {
            char buf[16];
            snprintf(buf, sizeof(buf), format_imperial, axis.dtg * INCH_PER_MM);
            RightJustifiedText(buf);
          }
          ImGui::PopStyleColor(1);
        }
      }

      ImGui::PopFont();
      ImGui::EndTable();
    }
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("ch2");
    if (ImGui::BeginTable("##tfstable", 3,
                          ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_BordersInnerV))
    {
      ImGui::TableSetupColumn("T,F,S", ImGuiTableColumnFlags_WidthFixed);
      if (position_display_metric) {
        ImGui::TableSetupColumn("");
      }

      ImGui::TableHeadersRow();

      // Tool
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::PushFont(io.Fonts->Fonts[4]);
      ImGui::Text("T");
      ImGui::TableNextColumn();
      ImGui::Text("%d", emcStatus->io.tool.toolInSpindle);
      ImGui::PopFont();
      //auto& tool = emcStatus->io.tool.toolTablecurrent
      ImGui::TableNextColumn();
      //auto& tool = emcStatus->io.tool.toolTable[emcStatus->io.tool.toolInSpindle];
      auto& tool = emcStatus->io.tool.toolTable[0];

      ImGui::Text("D %.3fmm", tool.diameter);
      ImGui::Text("L %.3fmm", tool.offset.tran.z);

      // Feedrate
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::PushFont(io.Fonts->Fonts[4]);
      ImGui::Text("F");
      ImGui::TableNextColumn();
      ImGui::Text("%.0f", emcStatus->motion.traj.tag.fields_float[1]);
      ImGui::PopFont();
      ImGui::TableNextColumn();
      ImGui::Text("%.0f", emcStatus->motion.traj.current_vel * 60);
      ImGui::Text("mm/min");

      // Spindle
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::PushFont(io.Fonts->Fonts[4]);
      ImGui::Text("S");
      ImGui::TableNextColumn();
      ImGui::Text("%.0f", emcStatus->motion.traj.tag.fields_float[2]);
      ImGui::PopFont();
      ImGui::TableNextColumn();
      ImGui::Text("%.0f", emcStatus->motion.spindle[0].speed);

      ImGui::EndTable();
    }
    ImGui::EndChild();
  }
  ImGui::End();
}

} // namespace ImCNC
