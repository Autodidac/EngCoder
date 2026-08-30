#include <string_view>

import epoch.gui.tile_workspace;

int main()
{
    using namespace epochengine::gui_lib::tile_workspace;
    static_assert(tool_name(Tool::select) == "Select");
    static_assert(tool_name(Tool::collision) == "Collision");
    return run_contract() == ContractFailure::none ? 0 : 1;
}
