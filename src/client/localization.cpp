#include "client/localization.h"

#include <cctype>

namespace nsync::client {
namespace {

struct Entry {
    std::string_view key;
    std::string_view english;
    std::string_view chinese;
};

constexpr Entry kEntries[] = {
    {"generic.on", "ON", "开"},
    {"generic.off", "OFF", "关"},
    {"generic.ready", "READY", "就绪"},
    {"generic.active", "ACTIVE", "激活"},
    {"generic.down", "DOWN", "阵亡"},
    {"generic.live", "LIVE", "存活"},
    {"generic.waiting", "WAITING", "等待中"},
    {"generic.offline", "OFFLINE", "离线"},
    {"generic.bot", "BOT", "机器人"},
    {"generic.open_slot", "OPEN SLOT", "空位"},
    {"generic.no_players", "NO PLAYERS", "无玩家"},
    {"generic.no_agents", "NO AGENTS LOADED", "未加载特工"},
    {"generic.waiting_players", "WAITING FOR PLAYERS", "等待玩家"},
    {"generic.unbound", "UNBOUND", "未绑定"},
    {"generic.press_key", "PRESS A KEY", "请按键"},
    {"generic.no_bindings", "NO BINDINGS", "暂无绑定"},
    {"menu.eyebrow", "OFFLINE SKIRMISH", "离线遭遇战"},
    {"menu.title", "DEPLOYMENT", "部署"},
    {"menu.description", "Pick an agent, choose a mode, and drop into the arena.", "选择特工，选择模式，进入竞技场。"},
    {"menu.agent", "AGENT", "特工"},
    {"menu.player", "PLAYER", "玩家"},
    {"menu.mode", "MODE", "模式"},
    {"menu.arena", "ARENA", "地图"},
    {"menu.tdm", "TDM", "团队死斗"},
    {"menu.tdm.description", "Team deathmatch. First to 30 or highest score at time.", "团队死斗。先到 30 分或时间结束时分数最高者获胜。"},
    {"menu.control", "CONTROL", "控制"},
    {"menu.control.description", "Hold the point. Capture, then score every 5 seconds.", "占领控制点。控制后每 5 秒得分。"},
    {"menu.crystal", "CRYSTAL", "水晶"},
    {"menu.crystal.description", "Grab crystals and deliver them to your depot.", "拾取水晶并带回己方收集点。"},
    {"menu.deploy", "DEPLOY", "部署"},
    {"menu.settings", "SETTINGS", "设置"},
    {"menu.quit", "QUIT", "退出"},
    {"menu.status", "AWAITING DEPLOYMENT", "等待部署"},
    {"menu.hp", "HP", "生命"},
    {"menu.shield", "SHIELD", "护盾"},
    {"menu.speed", "SPEED", "速度"},
    {"menu.choose", "Choose a mode and deploy.", "选择模式并部署。"},
    {"lobby.eyebrow", "SERVER AUTHORITATIVE", "服务器权威"},
    {"lobby.title", "WAR ROOM", "作战室"},
    {"lobby.connecting", "CONNECTING", "连接中"},
    {"lobby.team.blue", "TEAM BLUE", "蓝队"},
    {"lobby.team.red", "TEAM RED", "红队"},
    {"lobby.your_agent", "YOUR AGENT", "你的特工"},
    {"lobby.waiting", "WAITING FOR SESSION", "等待会话"},
    {"lobby.ready", "READY", "准备"},
    {"lobby.cancel_ready", "CANCEL READY", "取消准备"},
    {"lobby.leave", "LEAVE", "离开"},
    {"lobby.controls", "R READY / 1 2 3 SELECT AGENT", "R 准备 / 1 2 3 选择特工"},
    {"lobby.footer", "ALL HUMANS MUST READY. EMPTY SLOTS ARE FILLED WITH BOTS.", "所有真人必须准备，空位由机器人填充。"},
    {"lobby.ready_suffix", "READY", "已准备"},
    {"lobby.starting_in", "STARTING IN", "即将开始"},
    {"lobby.you", "YOU", "你"},
    {"team.blue", "BLUE", "蓝队"},
    {"hero.scout", "SCOUT", "侦察兵"},
    {"hero.gunner", "GUNNER", "机枪手"},
    {"hero.assault", "ASSAULT", "突击兵"},
    {"weapon.rifle", "RIFLE", "步枪"},
    {"weapon.shotgun", "SHOTGUN", "霰弹枪"},
    {"weapon.rocket", "ROCKET", "火箭筒"},
    {"weapon.smg", "SMG", "冲锋枪"},
    {"weapon.burst", "LASER", "激光枪"},
    {"weapon.grenade", "GRENADE", "榴弹发射器"},
    {"team.red", "RED", "红队"},
    {"hud.scoreboard", "SCOREBOARD", "计分板"},
    {"hud.player", "PLAYER", "玩家"},
    {"hud.hero", "HERO", "英雄"},
    {"hud.k", "K", "K"},
    {"hud.d", "D", "D"},
    {"hud.dmg", "DMG", "伤害"},
    {"hud.block", "BLOCK", "格挡"},
    {"hud.hp", "HP", "生命"},
    {"hud.status", "STATUS", "状态"},
    {"hud.tab_close", "TAB TO CLOSE", "TAB 关闭"},
    {"hud.eliminated", "ELIMINATED", "已阵亡"},
    {"hud.control_point", "CONTROL POINT", "控制点"},
    {"hud.crystal_run", "CRYSTAL RUN", "水晶争夺"},
    {"hud.crystal_deliver", "DELIVERING", "交付中"},
    {"hud.neutral", "NEUTRAL", "中立"},
    {"hud.blue_hold", "BLUE HOLD", "蓝队控制"},
    {"hud.red_hold", "RED HOLD", "红队控制"},
    {"hud.blue_capturing", "BLUE CAPTURING", "蓝队占领中"},
    {"hud.red_capturing", "RED CAPTURING", "红队占领中"},
    {"hud.respawn_in", "RESPAWN IN", "复活倒计时"},
    {"hud.waiting_respawn", "WAITING TO RESPAWN", "等待复活"},
    {"hud.shocked", "SHOCKED", "被电击"},
    {"pause.eyebrow", "SIMULATION PAUSED", "模拟已暂停"},
    {"pause.title", "PAUSED", "已暂停"},
    {"pause.description", "The local simulation is frozen. Resume when you are ready.", "本地模拟已冻结，准备好后继续。"},
    {"pause.resume", "RESUME", "继续"},
    {"pause.settings", "SETTINGS", "设置"},
    {"pause.menu", "MAIN MENU", "主菜单"},
    {"pause.quit", "QUIT TO DESKTOP", "退出桌面"},
    {"results.online", "ONLINE MATCH", "在线对局"},
    {"results.offline", "OFFLINE SKIRMISH", "离线遭遇战"},
    {"results.match_over", "MATCH OVER", "对局结束"},
    {"results.draw", "DRAW", "平局"},
    {"results.victory", "VICTORY", "胜利"},
    {"results.defeat", "DEFEAT", "失败"},
    {"results.complete", "MATCH COMPLETE", "对局完成"},
    {"results.team_wins", "WINS THE ARENA", "赢得竞技场"},
    {"results.rematch", "REMATCH", "再来一局"},
    {"results.menu", "MAIN MENU", "主菜单"},
    {"results.quit", "QUIT", "退出"},
    {"settings.eyebrow", "CLIENT CONFIGURATION", "客户端配置"},
    {"settings.title", "SETTINGS", "设置"},
    {"settings.tab.game", "GAME", "游戏"},
    {"settings.tab.controls", "CONTROLS", "控制"},
    {"settings.close", "ESC TO CLOSE", "ESC 关闭"},
    {"settings.audio", "AUDIO", "音频"},
    {"settings.sound_effects", "SOUND EFFECTS", "音效"},
    {"settings.sound_desc", "Master switch for weapon, impact and UI cues", "武器、命中和界面提示音的总开关"},
    {"settings.master_volume", "MASTER VOLUME", "主音量"},
    {"settings.audio_note", "Click a segment to set volume. Music and announcer tracks are not available yet.", "点击分段设置音量。音乐和播报音轨暂未提供。"},
    {"settings.video", "VIDEO", "视频"},
    {"settings.resolution", "RESOLUTION", "分辨率"},
    {"settings.prev", "PREV", "上一个"},
    {"settings.next", "NEXT", "下一个"},
    {"settings.fullscreen", "FULLSCREEN", "全屏"},
    {"settings.fullscreen_desc", "Use the selected mode on the primary display", "在主显示器使用所选模式"},
    {"settings.vsync", "VSYNC", "垂直同步"},
    {"settings.vsync_desc", "Display synchronization", "显示器同步"},
    {"settings.blood", "BLOOD EFFECTS", "血液特效"},
    {"settings.blood_desc", "Red health-hit flashes and death burst", "红色受击闪烁和死亡爆散"},
    {"settings.key_bindings", "KEY BINDINGS", "按键绑定"},
    {"settings.key_help", "Click a binding, then press a key. Backspace resets it; Escape cancels. Mouse fire is fixed to LMB.", "点击绑定后按键。Backspace 重置，Escape 取消；鼠标开火固定为左键。"},
    {"settings.reset", "RESET DEFAULTS", "恢复默认"},
    {"settings.cancel", "CANCEL", "取消"},
    {"settings.apply", "APPLY", "应用"},
    {"settings.audio_off", "AUDIO OFF", "音效关闭"},
    {"settings.audio_ready", "AUDIO READY", "音频就绪"},
    {"settings.audio_unavailable", "AUDIO UNAVAILABLE", "音频不可用"},
    {"settings.changes_apply", "CHANGES APPLY ON APPLY", "修改后点击应用"},
    {"settings.unsaved", "UNSAVED CHANGES", "未应用修改"},
    {"settings.defaults_restored", "DEFAULTS RESTORED", "已恢复默认"},
    {"settings.volume", "VOLUME", "音量"},
    {"settings.press_key_for", "PRESS A KEY FOR", "请按键设置"},
    {"settings.binding_cancelled", "BINDING CANCELLED", "绑定已取消"},
    {"settings.binding_reset", "BINDING RESET", "绑定已重置"},
    {"settings.binding_updated", "BINDING UPDATED", "绑定已更新"},
    {"settings.f5_reserved", "F5 IS RESERVED FOR ASSET RELOAD", "F5 保留给资源重载"},
    {"settings.key_bound", "KEY ALREADY BOUND TO", "按键已绑定到"},
    {"settings.save_failed", "SAVE FAILED; CHECK CONFIG PATH", "保存失败，请检查配置路径"},
    {"settings.language", "LANGUAGE", "语言"},
    {"settings.language_desc", "UI language placeholder", "界面语言占位"},
    {"settings.language_toggle", "TOGGLE LANGUAGE", "切换语言"},
    {"language.english", "ENGLISH", "英文"},
    {"language.chinese", "CHINESE", "中文"},
    {"loading.kicker", "CLIENT BOOT", "客户端启动"},
    {"loading.error_kicker", "CLIENT ERROR", "客户端错误"},
    {"loading.resources", "CHECKING RESOURCES", "检查资源"},
    {"loading.content", "LOADING CONTENT", "加载内容"},
    {"loading.ready", "ARENA READY", "竞技场就绪"},
    {"loading.detail", "RMLUI // SDL3 // BGFX", "RMLUI // SDL3 // BGFX"},
    {"loading.preparing", "PREPARING ARENA", "准备竞技场"},
    {"loading.spawning", "SPAWNING AGENTS", "生成特工"},
    {"loading.starting", "STARTING MATCH", "开始比赛"},
    {"loading.deploy_detail", "MAP // LOCAL SIMULATION", "地图 // 本地模拟"},
    {"loading.ok", "OK", "确定"},
    {"error.control_objective", "CONTROL mode requires a map objective.", "控制模式需要地图目标点。"},
    {"error.crystal_layout", "CRYSTAL mode requires crystal spawns and depots.", "水晶模式需要水晶出生点和收集点。"},
    {"action.left", "MOVE LEFT", "左移"},
    {"action.right", "MOVE RIGHT", "右移"},
    {"action.moveleft", "MOVE LEFT", "左移"},
    {"action.moveright", "MOVE RIGHT", "右移"},
    {"action.jump", "JUMP", "跳跃"},
    {"action.reload", "RELOAD", "换弹"},
    {"action.weapon", "SWITCH WEAPON", "切换武器"},
    {"action.weaponswitch", "SWITCH WEAPON", "切换武器"},
    {"action.ability", "ABILITY", "技能"},
    {"action.gadget1", "SKILL 1", "技能 1"},
    {"action.gadget2", "SKILL 2", "技能 2"},
    {"action.gadget3", "SKILL 3", "技能 3"},
    {"action.scoreboard", "SCOREBOARD", "计分板"},
    {"action.pause", "PAUSE", "暂停"},
};

} // namespace

UiLanguage parse_language(std::string_view code) {
    std::string normalized;
    normalized.reserve(code.size());
    for (const char character : code) {
        normalized.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(character))));
    }
    if (normalized == "zh" || normalized == "zh-cn" ||
        normalized == "cn" || normalized == "chinese") {
        return UiLanguage::Chinese;
    }
    return UiLanguage::English;
}

const char* language_code(UiLanguage language) {
    return language == UiLanguage::Chinese ? "zh" : "en";
}

const char* language_label(UiLanguage language) {
    return language == UiLanguage::Chinese ? "中文" : "ENGLISH";
}

std::string Localization::text(std::string_view key) const {
    for (const Entry& entry : kEntries) {
        if (entry.key != key) {
            continue;
        }
        if (language_ == UiLanguage::Chinese && !entry.chinese.empty()) {
            return std::string(entry.chinese);
        }
        return std::string(entry.english);
    }
    return "[" + std::string(key) + "]";
}

std::string Localization::text_or(
    std::string_view key,
    std::string_view fallback) const {
    for (const Entry& entry : kEntries) {
        if (entry.key != key) {
            continue;
        }
        if (language_ == UiLanguage::Chinese && !entry.chinese.empty()) {
            return std::string(entry.chinese);
        }
        return std::string(entry.english);
    }
    return std::string(fallback);
}

} // namespace nsync::client
