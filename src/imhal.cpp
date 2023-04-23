// clang-format off
#include "hal.h"
#include "../src/hal/hal_priv.h"
// clang-format on

#include "imgui.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#include <array>
#include <string>
#include <vector>

namespace ImCNC {

static int comp_id = 0;

void exit_from_hal()
{
  rtapi_mutex_give(&(hal_data->mutex));
  hal_exit(comp_id);
}

void initHAL()
{
  char buf[30];
  snprintf(buf, 29, "imcnc-%d", getpid());
  comp_id = hal_init(buf);

  // register an exit function
  atexit(exit_from_hal);
}

static unsigned calc_level(const char* pin_name, const char*& name)
{
  unsigned level = 0;

  while (*pin_name) {
    if (*pin_name == '.') {
      name = pin_name + 1;
      level++;
    }
    pin_name++;
  }
  return level;
}

static unsigned calc_level_diff(const char* a, const char* b)
{
  unsigned level = 0;

  while (*a && *b && *a == *b) {
    if (*a == '.')
      level++;
    a++;
    b++;
  }
  return level;
}

static std::string get_name_level(const char* name, unsigned level)
{
  const char* begin = name;
  const char* end = name;

  while (*end && *end != '.')
    end++;

  for (unsigned i = level; i > 0 && *end; i--) {
    begin = end + 1;
    end = begin;
    while (*end && *end != '.')
      end++;
  }
  return std::string{begin, end};
}

void ShowHAL()
{
  ImGui::Begin("HAL Pins");
  // get mutex
  rtapi_mutex_get(&(hal_data->mutex));

  if (ImGui::CollapsingHeader("Components")) {
    auto next = hal_data->comp_list_ptr;

    while (next != 0) {
      hal_comp_t* comp = static_cast<hal_comp_t*>(SHMPTR(next));
      const char* type = "unknown";
      if (comp->type == COMPONENT_TYPE_USER)
        type = "user";
      else if (comp->type == COMPONENT_TYPE_REALTIME)
        type = "realtime";
      else if (comp->type == COMPONENT_TYPE_OTHER)
        type = "other";

      ImGui::Text("%s-%d (%d) %s", comp->name, comp->comp_id, comp->pid, type);
      next = comp->next_ptr;
    }
  }

  if (ImGui::CollapsingHeader("Pins")) {
    auto next = hal_data->pin_list_ptr;
    hal_pin_t* last_pin = 0;
    unsigned llv = 0, open_level = 0;

    while (next != 0) {
      hal_pin_t* pin = static_cast<hal_pin_t*>(SHMPTR(next));

      const char* name = "";
      auto lv = calc_level(pin->name, name);
      auto slv = calc_level_diff(pin->name, last_pin ? last_pin->name : "");

      // unindent llv - slv levels
      while (llv > slv) {
        if (open_level >= llv) {
          ImGui::TreePop();
          open_level--;
        }
        llv--;
      }

      bool disp = (slv == open_level);
      // indent lv - slv levels
      while (lv > slv && open_level >= slv) {
        // get name
        std::string s = get_name_level(pin->name, open_level);
        disp = ImGui::TreeNode(pin->name, "%s", s.c_str());
        if (disp) {
          open_level++;
          slv++;
        }
        else
          break;
      }

      void* value_ptr;

      if (pin->signal) {
        hal_sig_t* sig = static_cast<hal_sig_t*>(SHMPTR(pin->signal));
        value_ptr = SHMPTR(sig->data_ptr);
      }
      else
        value_ptr = &(pin->dummysig);

      if (disp) {
        switch (pin->type) {
        case HAL_BIT:
          ImGui::Text("%d %s[bit]: %d", slv, name,
                      *static_cast<char*>(value_ptr));
          break;
        case HAL_S32:
          ImGui::Text("%d %s[s32]: %d", slv, name,
                      *static_cast<int*>(value_ptr));
          break;
        case HAL_U32:
          ImGui::Text("%d %s[u32]: %d", slv, name,
                      *static_cast<unsigned*>(value_ptr));
          break;
        case HAL_FLOAT:
          ImGui::Text("%d %s[f64]: %f", slv, name,
                      *static_cast<double*>(value_ptr));
          break;
        default:
          break;
        }
      }
      next = pin->next_ptr;
      last_pin = pin;
      llv = lv;
    }

    // unindent llv - slv levels
    while (open_level > 0) {
      ImGui::TreePop();
      open_level--;
    }
  }

  if (ImGui::CollapsingHeader("Signals")) {
    auto next = hal_data->sig_list_ptr;

    while (next != 0) {
      hal_sig_t* sig = static_cast<hal_sig_t*>(SHMPTR(next));

      if (ImGui::TreeNode(sig->name)) {
        ImGui::Text("Type: %d", sig->type);
        ImGui::Text("Readers: %d", sig->readers);
        ImGui::Text("Writers: %d", sig->writers);
        ImGui::Text("BiDirs: %d", sig->bidirs);

        ImGui::TreePop();
      }

      next = sig->next_ptr;
    }
  }

  if (ImGui::CollapsingHeader("Parameters")) {
    auto next = hal_data->param_list_ptr;
    hal_param_t* last_param = 0;
    unsigned llv = 0, open_level = 0;

    while (next != 0) {
      hal_param_t* param = static_cast<hal_param_t*>(SHMPTR(next));

      const char* name = "";
      auto lv = calc_level(param->name, name);
      auto slv = calc_level_diff(param->name, last_param ? last_param->name : "");

      // unindent llv - slv levels
      while (llv > slv) {
        if (open_level >= llv) {
          ImGui::TreePop();
          open_level--;
        }
        llv--;
      }

      bool disp = (slv == open_level);
      // indent lv - slv levels
      while (lv > slv && open_level >= slv) {
        // get name
        std::string s = get_name_level(param->name, open_level);
        disp = ImGui::TreeNode(param->name, "%s", s.c_str());
        if (disp) {
          open_level++;
          slv++;
        }
        else
          break;
      }

      void* value_ptr = SHMPTR(param->data_ptr);
      if (disp) {
        switch (param->type) {
        case HAL_BIT:
          ImGui::Text("%d %s[bit]: %d", slv, name,
                      *static_cast<char*>(value_ptr));
          break;
        case HAL_S32:
          ImGui::Text("%d %s[s32]: %d", slv, name,
                      *static_cast<int*>(value_ptr));
          break;
        case HAL_U32:
          ImGui::Text("%d %s[u32]: %d", slv, name,
                      *static_cast<unsigned*>(value_ptr));
          break;
        case HAL_FLOAT:
          ImGui::Text("%d %s[f64]: %f", slv, name,
                      *static_cast<double*>(value_ptr));
          break;
        default:
          break;
        }
      }
      next = param->next_ptr;
      last_param = param;
      llv = lv;
    }

    // unindent llv - slv levels
    while (open_level > 0) {
      ImGui::TreePop();
      open_level--;
    }
  }

  if (ImGui::CollapsingHeader("Functions")) {
    auto next = hal_data->funct_list_ptr;

    while (next != 0) {
      hal_funct_t* funct = static_cast<hal_funct_t*>(SHMPTR(next));

      if (ImGui::TreeNode(funct->name, "%s %d", funct->name, funct->maxtime)) {
        hal_comp_t* comp = static_cast<hal_comp_t*>(SHMPTR(funct->owner_ptr));

        if (funct->uses_fp)
          ImGui::Text("uses floating point");
        if (funct->reentrant)
          ImGui::Text("reentrant");
        ImGui::Text("owner %s", comp->name);
        ImGui::Text("runtime %p", funct->runtime);
        ImGui::Text("maxtime %d", funct->maxtime);
        ImGui::TreePop();
      }

      next = funct->next_ptr;
    }
  }

  if (ImGui::CollapsingHeader("Threads")) {
    auto next = hal_data->thread_list_ptr;

    while (next != 0) {
      hal_thread_t* thread = static_cast<hal_thread_t*>(SHMPTR(next));

      ImGui::Text("%s(%d) %ld(%d) ", thread->name, thread->task_id, thread->period, thread->priority);

      next = thread->next_ptr;
    }
  }

  rtapi_mutex_give(&(hal_data->mutex));
  ImGui::End();
}

} // namespace ImCNC
