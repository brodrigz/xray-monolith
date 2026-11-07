-- Run from repository root with the standalone LuaJIT runner.
local command_tokens = {
    r_upscaler = {"off", "dlss", "fsr3"},
    r_fsr3_quality = {"native_aa", "quality", "balanced", "performance", "ultra_performance"},
    r_dlss_quality = {"off", "dlaa", "quality", "balanced", "performance", "ultra_performance"},
    r_dlss_preset = {"default", "j", "k", "l", "m"},
    r_upscaler_sunshafts = {"upscaled", "smoothed", "raw"},
}
local live = {r_upscaler = "dlss", r_dlss_quality = "quality", r_fsr3_quality = "balanced",
    r_dlss_preset = "k", r_dlss_sharpness = 0.35, r_upscaler_post_after = "1",
    r_upscaler_sunshafts = "smoothed", r_upscaler_particles_after = "0", r_dlss_hdr_input = "1",
    r2_sunshafts_mode = "screen_space"}
local SWITCHES = "upscaler_post_after,upscaler_sunshafts,upscaler_particles_after"
local pending, backup, writes = {}, {}, 0
local coded, base_calls, renders = 0, 0, 0
function get_console()
    return {get_token_list = function(_, name) return command_tokens[name] or {} end}
end
function get_console_cmd(kind, command)
    if kind == 1 then return live[command] == "1" end
    return live[command]
end
function exec_console_cmd(command)
    local key, value = command:match("^(%S+) (.+)$")
    assert(key and live[key] ~= nil, command)
    live[key] = value
    writes = writes + 1
end
ui_options = {UIOptions = {}}
local class = ui_options.UIOptions
function class:GetOption(id)
    local opt = id:match("[^/]+$")
    local page = id:find("/advanced/", 1, true) and 2 or 1
    for _, row in ipairs(ui_options.options[1].gr[page].gr) do
        if row.id == opt then return row end
    end
    return {}
end
function class:GetCurrentValue(path, opt, row)
    local id = path .. "/" .. opt
    if pending[id] ~= nil then return pending[id] end
    if row.curr then return row.curr[1]() end
    return live[row.cmd]
end
function class:CacheValue(path, opt, value, row)
    local id = path .. "/" .. opt
    if backup[id] == nil then backup[id] = self:GetCurrentValue(path, opt, row) end
    pending[id] = value ~= backup[id] and value or nil
end
function class:Reset_opt(tree, path, flags)
    renders = renders + 1
    self.visible, self.values = {}, {}
    for _, row in ipairs(tree.gr) do
        self.visible[#self.visible + 1] = row.id
        self.values[row.id] = self:GetCurrentValue(path, row.id, row)
    end
end
function class:Update() self.base_updates = (self.base_updates or 0) + 1 end
function class:On_Accept()
    for id, value in pairs(pending) do
        local row = self:GetOption(id)
        assert(row.cmd, id)
        if type(value) == "boolean" then
            assert(row.bool_to_num, id)
            value = value and "1" or "0"
        end
        exec_console_cmd(row.cmd .. " " .. tostring(value))
    end
    pending, backup = {}, {}
end
-- Exercise the audited GAMMA framework's real pending-value/cache functions,
-- not just mocks, when the staged options script is available locally.
local framework = io.open("_build/dlss_game_test/ui-stage/scripts/ui_options.script", "r")
if framework then
    local source = framework:read("*a")
    framework:close()
    local framework_env = setmetatable({
        UIOptions = class, _opt_ = "/", s_gsub = string.gsub,
        cc = function(path, opt) return path .. "/" .. opt end,
        print_dbg = function() end,
        exec = function(fn, ...) return fn(...) end,
        clamp = function(v, lo, hi) return math.max(lo, math.min(hi, v)) end,
        round_idp = function(v) return v end,
        axr_main = {config = {r_value = function() end}},
    }, {__index = function(_, key)
        if key == "opt_temp" then return pending end
        if key == "opt_backup" then return backup end
        return _G[key]
    end})
    function class:GetValue(path, opt, row) return self:GetCurrentValue(path, opt, row) end
    function class:UpdatePending() end
    for _, name in ipairs({"GetCurrentValue", "CacheValue"}) do
        local body = assert(source:match("function UIOptions:" .. name .. "%(.-\nend"))
        local method = assert(loadstring(body))
        setfenv(method, framework_env)
        method()
    end
    print("Using GAMMA's real GetCurrentValue/CacheValue for pending-state tests.")
end
ui_options.init_opt_coder = function() coded = coded + 1 end
ui_options.init_opt_base = function()
    base_calls = base_calls + 1
    ui_options.options = {{id = "video", gr = {{id = "basic", gr = {
        {id = "renderer"}, {id = "resolution"}, {id = "fov"},
    }}, {id = "advanced", gr = {
        {id = "sunshafts_mode", val = 0, cmd = "r2_sunshafts_mode"},
    }}}}}
    ui_options.init_opt_coder()
end
local env = setmetatable({}, {__index = _G})
local chunk = assert(loadfile("gamedata/scripts/modxml_dlss_options.script"))
setfenv(chunk, env)
chunk()
env.on_xml_read()
local menu = setmetatable({_Cap = {}}, {__index = class})
local function open()
    menu.last_curr_tree = ui_options.options[1].gr[1]
    menu.last_path = "video/basic"
    menu:Reset_opt(menu.last_curr_tree, menu.last_path)
end
local function expect(ids)
    assert(table.concat(menu.visible, ",") == "renderer,resolution,fov,line,title," .. ids,
        table.concat(menu.visible, ","))
    assert(#ui_options.options[1].gr[1].gr == 14, "Filtering mutated canonical tree")
end
local function change(opt, value)
    local row = menu:GetOption("video/basic/" .. opt)
    menu:CacheValue("video/basic", opt, value, row)
end
local function select_method(method)
    local old_renders = renders
    change("upscaler", method)
    assert(renders == old_renders, "Destroyed controls inside selection callback")
    menu:Update()
    assert(renders == old_renders + 1)
end
local function discard()
    pending, backup = {}, {}
    open()
end
open()
expect("upscaler,dlss_mode,dlss_preset,dlss_linear_input,dlss_sharpness," .. SWITCHES)
local quality = menu:GetOption("video/basic/dlss_mode")
assert(#quality.content == 5 and quality.content[1][1] == "dlaa")
for _, entry in ipairs(quality.content) do assert(entry[1] ~= "off") end
for _, id in ipairs({"upscaler", "dlss_mode", "fsr3_mode", "dlss_preset"}) do
    local row = menu:GetOption("video/basic/" .. id)
    assert(row.vid and not row.restart and not row.functor)
end
local sharp = menu:GetOption("video/basic/dlss_sharpness")
assert(sharp.min == 0 and sharp.max == 1 and sharp.step == 0.05 and not sharp.vid)
change("dlss_mode", "performance")
change("dlss_preset", "m")
select_method("fsr3")
expect("upscaler,fsr3_mode,dlss_sharpness," .. SWITCHES)
assert(menu.values.fsr3_mode == "balanced")
change("fsr3_mode", "ultra_performance")
change("dlss_sharpness", 0.5)
select_method("dlss")
expect("upscaler,dlss_mode,dlss_preset,dlss_linear_input,dlss_sharpness," .. SWITCHES)
assert(menu.values.dlss_mode == "performance" and menu.values.dlss_preset == "m")
select_method("fsr3")
assert(menu.values.fsr3_mode == "ultra_performance" and menu.values.dlss_sharpness == 0.5)
select_method("off")
expect("upscaler")
assert(writes == 0, "Selection wrote to live console before Apply")
discard()
expect("upscaler,dlss_mode,dlss_preset,dlss_linear_input,dlss_sharpness," .. SWITCHES)
assert(menu.values.dlss_mode == "quality" and menu.values.dlss_preset == "k" and writes == 0)
select_method("fsr3")
change("fsr3_mode", "native_aa")
menu:On_Accept()
assert(live.r_upscaler == "fsr3" and live.r_fsr3_quality == "native_aa")
open()
expect("upscaler,fsr3_mode,dlss_sharpness," .. SWITCHES)
-- Legacy config: DLSS selected, but disabled via quality=off.
live.r_upscaler, live.r_dlss_quality = "dlss", "off"
open()
expect("upscaler")
local before = writes
select_method("dlss")
expect("upscaler,dlss_mode,dlss_preset,dlss_linear_input,dlss_sharpness," .. SWITCHES)
assert(menu.values.dlss_mode == "quality" and live.r_dlss_quality == "off" and writes == before)
discard()
expect("upscaler")
assert(live.r_dlss_quality == "off" and writes == before)
select_method("dlss")
change("dlss_mode", "balanced")
select_method("off")
expect("upscaler")
menu:On_Accept()
assert(live.r_upscaler == "off" and live.r_dlss_quality == "balanced", "Off accidentally enabled DLSS")
select_method("dlss")
assert(menu.values.dlss_mode == "balanced", "Lost saved quality")
menu:On_Accept()
assert(live.r_upscaler == "dlss")
-- Navigating elsewhere before the deferred update must not reopen Basic.
change("upscaler", "fsr3")
menu.last_path = "audio/general"
menu:Reset_opt({gr = {{id = "volume"}}}, menu.last_path)
menu:Update()
assert(menu.visible[1] == "volume" and #menu.visible == 1)
discard()
env.on_xml_read()
assert(base_calls == 1 and coded == 1)
ui_options.init_opt_base()
open()
expect("upscaler,dlss_mode,dlss_preset,dlss_linear_input,dlss_sharpness," .. SWITCHES)
assert(base_calls == 2 and coded == 2)
ui_options.init_opt_coder()
assert(#ui_options.options[1].gr[1].gr == 14 and coded == 3)
-- The section: switches follow the upscaler and the post chain.
assert(menu:GetOption("video/basic/dlss_linear_input").cmd == "r_dlss_hdr_input")
assert(menu:GetOption("video/basic/dlss_linear_input").vid)
assert(menu:GetOption("video/basic/title").text == "ui_mm_header_upscaling")
assert(menu:GetOption("video/basic/upscaler_post_after").vid)
assert(not menu:GetOption("video/basic/upscaler_particles_after").vid)
local function toggle_post_after(value)
    local old_renders = renders
    change("upscaler_post_after", value)
    assert(renders == old_renders, "Destroyed controls inside selection callback")
    menu:Update()
    assert(renders == old_renders + 1)
end
live.r_upscaler, live.r_dlss_quality = "dlss", "quality"
open()
toggle_post_after(false)
expect("upscaler,dlss_mode,dlss_preset,dlss_linear_input,dlss_sharpness,upscaler_post_after")
assert(live.r_upscaler_post_after == "1", "Toggle wrote to live console before Apply")
menu:On_Accept()
assert(live.r_upscaler_post_after == "0")
open()
expect("upscaler,dlss_mode,dlss_preset,dlss_linear_input,dlss_sharpness,upscaler_post_after")
toggle_post_after(true)
expect("upscaler,dlss_mode,dlss_preset,dlss_linear_input,dlss_sharpness," .. SWITCHES)
menu:On_Accept()
assert(live.r_upscaler_post_after == "1")
-- Sunshaft handling follows the sunshaft type, including its pending value on the Advanced page.
local function set_sunshaft_type(kind)
    menu:CacheValue("video/advanced", "sunshafts_mode", kind, menu:GetOption("video/advanced/sunshafts_mode"))
    open()
end
local function handling_values()
    local values = {}
    for _, entry in ipairs(menu:GetOption("video/basic/upscaler_sunshafts").content) do
        values[#values + 1] = entry[1]
    end
    return table.concat(values, ",")
end
open()
assert(handling_values() == "upscaled,smoothed,raw" and menu.values.upscaler_sunshafts == "smoothed")
set_sunshaft_type("off")
expect("upscaler,dlss_mode,dlss_preset,dlss_linear_input,dlss_sharpness,upscaler_post_after,upscaler_particles_after")
set_sunshaft_type("volumetric")
expect("upscaler,dlss_mode,dlss_preset,dlss_linear_input,dlss_sharpness," .. SWITCHES)
-- Volumetric sunshafts are smoothed too: the same choices for every type.
assert(handling_values() == "upscaled,smoothed,raw" and menu.values.upscaler_sunshafts == "smoothed")
set_sunshaft_type("combined")
assert(handling_values() == "upscaled,smoothed,raw")
discard()
assert(handling_values() == "upscaled,smoothed,raw" and menu.values.upscaler_sunshafts == "smoothed")
change("upscaler_sunshafts", "upscaled")
menu:On_Accept()
assert(live.r_upscaler_sunshafts == "upscaled")
change("upscaler_sunshafts", "smoothed")
menu:On_Accept()
assert(live.r_upscaler_sunshafts == "smoothed")
open()
-- Older DLSS-only binary keeps the old controls, including quality=off.
command_tokens.r_upscaler, command_tokens.r_fsr3_quality = nil, nil
command_tokens.r_upscaler_sunshafts = nil
live.r_upscaler_post_after, live.r_upscaler_particles_after, live.r_dlss_hdr_input = nil, nil, nil
ui_options.init_opt_base()
open()
assert(table.concat(menu.visible, ",") == "renderer,resolution,fov,line,title,dlss_mode,dlss_preset,dlss_sharpness")
assert(menu:GetOption("video/basic/dlss_mode").content[1][1] == "off")
command_tokens = {}
ui_options.init_opt_base()
open()
assert(table.concat(menu.visible, ",") == "renderer,resolution,fov")
menu:On_Accept() -- no attempt to reconcile nonexistent upscaler controls
print("Dynamic upscaler UI tests passed: DLSS/FSR/Off visibility, deferred refresh, independent pending qualities, Apply/Cancel, legacy Off, navigation, rebuild and unsupported renderers.")
