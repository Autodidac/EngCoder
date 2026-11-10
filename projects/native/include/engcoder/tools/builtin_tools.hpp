#pragma once
#include "engcoder/core/config.hpp"
#include "engcoder/model/lm_studio_provider.hpp"
#include "engcoder/addons/addon_registry.hpp"
#include "engcoder/persistence/session_store.hpp"
#include "engcoder/tools/tool_registry.hpp"
#include "engcoder/workspace/checkpoint_store.hpp"
#include "engcoder/workspace/workspace.hpp"

namespace engcoder::tools {
void register_builtin_tools(ToolRegistry&, workspace::Workspace&, workspace::CheckpointStore&, persistence::SessionStore&, model::LmStudioProvider&, addons::AddonRegistry&, const core::Config&);
}
