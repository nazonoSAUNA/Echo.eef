#include <windows.h>
#include <algorithm>
#include <exedit.hpp>

static char name[] = "エコー";

constexpr int track_n = 6;
static char* track_name[track_n] = { const_cast<char*>("ミックス"), const_cast<char*>("強さ-左"), const_cast<char*>("遅延-左"), const_cast<char*>("クロス"), const_cast<char*>("強さ-右"), const_cast<char*>("遅延-右") };

constexpr int track_id_mix = 0;
constexpr int track_id_strength = 1;
constexpr int track_id_strength_l = 1;
constexpr int track_id_delay = 2;
constexpr int track_id_delay_l = 2;
constexpr int track_id_cross = 3;
constexpr int track_id_strength_r = 4;
constexpr int track_id_delay_r = 5;

static int track_default[track_n] = { 500, 600, 200, 250, 600, 200 };
static int track_s[track_n] = { 0, 0, 1, 0, 0, 1 };
static int track_e[track_n] = { 1000, 1000, 1000, 1000, 1000, 1000 };
static int track_scale[track_n] = { 10, 10, 1, 10, 10, 1 };
static int track_drag_min[track_n] = { 0, 100, 50, 0, 100, 50 };
static int track_drag_max[track_n] = { 1000, 900, 1000, 1000, 900, 1000 };

constexpr int check_n = 1;
constexpr int type_n = 3;
static char* check_name[check_n] = { const_cast<char*>("ディレイ\0ステレオディレイ\0ピンポンディレイ\0") };
static int check_default[check_n] = { -2 };

constexpr int exdata_size = 4;
static char exdata_def[exdata_size] = { 0, 0, 0, 0 };
static ExEdit::ExdataUse exdata_use[] = {
    {
        .type = ExEdit::ExdataUse::Type::Number,
        .size = 4,
        .name = "type",
    },
};

static char* track_disp_monaural[track_n] = { const_cast<char*>("ミックス"), const_cast<char*>("強さ"), const_cast<char*>("遅延(ms)"), const_cast<char*>("クロス"), const_cast<char*>("-"), const_cast<char*>("-") };
static char** track_disp[type_n] = {
    track_disp_monaural, // ディレイ
    track_name, // ステレオディレイ
    track_disp_monaural, // ピンポンディレイ
};

struct Exdata {
    int type;
};

DWORD get_func_address(DWORD call_address) {
    return 4 + call_address + *reinterpret_cast<DWORD*>(call_address);
}
static void* (__cdecl* get_or_create_cache)(ExEdit::ObjectFilterIndex, int, int, int, int, int*) = nullptr;
static void(__cdecl* update_any_exdata)(ExEdit::ObjectFilterIndex, const char*) = nullptr;

int get_roundup_power_of_2(int n) {
    --n;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    return ++n;
}


BOOL func_proc(ExEdit::Filter* efp, ExEdit::FilterProcInfo* efpip) {
    constexpr int min_inv_speed = 4;

    struct { // キャッシュは16byte増しで生成されるのでint[4]までは安全に追加できる
        int offset;
        int frame;
        int data[1];
    }*cache;
    int buffer_size = get_roundup_power_of_2(std::clamp(efpip->audio_ch * efpip->audio_rate * efp->track_e[track_id_delay_l] / 1000 * min_inv_speed, 0x10000, 0x8000000));
    int cache_exists_flag;
    cache = (decltype(cache))get_or_create_cache(efp->processing, buffer_size, 1, sizeof(*cache->data) * 8, 0, &cache_exists_flag); // クロスフェードで正しく動作させるために引数のefpip->v_func_idx → 0にしてみる
    if (cache == NULL) {
        return TRUE;
    }
    int frame = efpip->frame + efpip->add_frame;
    if (frame == 0 || frame < cache->frame || cache_exists_flag == 0) {
        memset(cache->data, 0, buffer_size * sizeof(*cache->data));
        cache->offset = 0;
        cache->frame = -1;
    }
    short* audiop;
    if ((byte)efp->flag & (byte)(ExEdit::Filter::Flag::Effect)) {
        audiop = efpip->audio_data;
    } else {
        audiop = efpip->audio_p;
    }

    int mix = std::clamp(efp->track[track_id_mix], efp->track_s[track_id_mix], efp->track_e[track_id_mix]);
    int dry = 0x1000;
    int wet = 0x1000;
    if (mix < 500) {
        wet = (mix << 10) / 125;
    } else if (500 < mix) {
        dry = ((1000 -mix) << 10) / 125;
    }
    int vol = (std::clamp(efp->track[track_id_strength_l], efp->track_s[track_id_strength_l], efp->track_e[track_id_strength_l]) << 9) / 125;
    int speed = efpip->audio_speed;
    if (speed == 0) {
        speed = 1000000;
    }
    speed = max(speed, 1000000 / min_inv_speed);
    int delay = (std::clamp(efp->track[track_id_delay_l], efp->track_s[track_id_delay_l], efp->track_e[track_id_delay_l]) * efpip->audio_rate / speed * 1000) * efpip->audio_ch;
    int cross = (std::clamp(efp->track[track_id_cross], efp->track_s[track_id_cross], efp->track_e[track_id_cross]) << 9) / 125;
    Exdata* exdata = reinterpret_cast<Exdata*>(efp->exdata_ptr);
    int type = exdata->type;
    if (efpip->audio_ch != 2) {
        cross = 0;
        type = 0;
    }
    int offset = cache->offset;
    if (cache->frame == frame) {
        offset -= efpip->audio_n * efpip->audio_ch;
        offset &= (buffer_size - 1);
    } else {
        cache->frame = frame;
    }
    switch (type) {
    case 0: {
        if (cross == 0) {
            for (int i = efpip->audio_n * efpip->audio_ch; 0 < i; i--) {
                cache->data[offset] = *audiop + ((cache->data[(offset - delay) & (buffer_size - 1)] * vol) >> 12);
                *audiop = std::clamp(cache->data[offset], SHRT_MIN, SHRT_MAX);
                audiop++;
                offset++; offset &= (buffer_size - 1);
            }
        } else {
            for (int i = efpip->audio_n; 0 < i; i--) {
                int la = audiop[0];
                int ra = audiop[1];

                int eff = ((cache->data[(offset - delay) & (buffer_size - 1)] * vol) >> 12);
                *audiop = std::clamp((la * dry + eff * wet) >> 12, SHRT_MIN, SHRT_MAX);
                cache->data[offset] = ((la << 12) + cross * ra) / (4096 + cross) + eff;
                audiop++;
                offset++;

                eff = ((cache->data[(offset - delay) & (buffer_size - 1)] * vol) >> 12);
                *audiop = std::clamp((ra * dry + eff * wet) >> 12, SHRT_MIN, SHRT_MAX);
                cache->data[offset] = ((ra << 12) + cross * la) / (4096 + cross) + eff;
                audiop++;
                offset++; offset &= (buffer_size - 1);
            }
        }
    }break;
    case 1: {
        int volr = (std::clamp(efp->track[track_id_strength_r], efp->track_s[track_id_strength_r], efp->track_e[track_id_strength_r]) << 9) / 125;
        int delayr = (std::clamp(efp->track[track_id_delay_r], efp->track_s[track_id_delay_r], efp->track_e[track_id_delay_r]) * efpip->audio_rate / 1000) * efpip->audio_ch;
        for (int i = efpip->audio_n; 0 < i; i--) {
            int la = audiop[0];
            int ra = audiop[1];

            int eff = ((cache->data[(offset - delay) & (buffer_size - 1)] * vol) >> 12);
            *audiop = std::clamp((la * dry + eff * wet) >> 12, SHRT_MIN, SHRT_MAX);
            cache->data[offset] = ((la << 12) + cross * ra) / (4096 + cross) + eff;
            audiop++;
            offset++;

            eff = ((cache->data[(offset - delayr) & (buffer_size - 1)] * volr) >> 12);
            *audiop = std::clamp((ra * dry + eff * wet) >> 12, SHRT_MIN, SHRT_MAX);
            cache->data[offset] = ((ra << 12) + cross * la) / (4096 + cross) + eff;
            audiop++;
            offset++; offset &= (buffer_size - 1);
        }
    }break;
    case 2: {
        for (int i = efpip->audio_n; 0 < i; i--) {
            int la = audiop[0];
            int ra = audiop[1];

            int eff = ((cache->data[(offset - delay) & (buffer_size - 1)] * vol) >> 12);
            *audiop = std::clamp((la * dry + eff * wet) >> 12, SHRT_MIN, SHRT_MAX);
            cache->data[offset + 1] = ((la << 12) + cross * ra) / (4096 + cross) + eff;
            audiop++;

            eff = ((cache->data[(offset + 1 - delay) & (buffer_size - 1)] * vol) >> 12);
            *audiop = std::clamp((ra * dry + eff * wet) >> 12, SHRT_MIN, SHRT_MAX);
            cache->data[offset] = ((ra << 12) + cross * la) / (4096 + cross) + eff;
            audiop++;
            offset += 2; offset &= (buffer_size - 1);
        }
    }break;
    }
    cache->offset = offset;
    return TRUE;
}
BOOL func_init(ExEdit::Filter* efp) {
    int exedit_dll_hinst = (int)efp->exedit_fp->dll_hinst;
    if (get_or_create_cache == nullptr) {
        // patch.aulにて音声ディレイのキャッシュを共有キャッシュに置き換える部分。同じ関数を読み込む
        (get_or_create_cache) = reinterpret_cast<decltype(get_or_create_cache)>(get_func_address(exedit_dll_hinst + 0x1c1ea));
    }
    if (update_any_exdata == nullptr) {
        (update_any_exdata) = reinterpret_cast<decltype(update_any_exdata)>(exedit_dll_hinst + 0x4a7e0);
    }

    return TRUE;
}
void update_extendedfilter_wnd(ExEdit::Filter* efp) {
    Exdata* exdata = reinterpret_cast<Exdata*>(efp->exdata_ptr);
    if (exdata->type < 0 || type_n <= exdata->type) return;

    SendMessageA(efp->exfunc->get_hwnd(efp->processing, 6, 0), CB_SETCURSEL, exdata->type, 0);
    SetWindowTextA(efp->exfunc->get_hwnd(efp->processing, 7, 0), "種類");
    for (int i = 0; i < track_n; i++) {
        auto track_hwnd = efp->exfunc->get_hwnd(efp->processing, 0, i);
        if (track_hwnd != nullptr) {
            SetWindowTextA(track_hwnd, track_disp[exdata->type][i]);
        }
    }
}

BOOL func_WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, AviUtl::EditHandle* editp, ExEdit::Filter* efp) {
    if (message == ExEdit::ExtendedFilter::Message::WM_EXTENDEDFILTER_COMMAND){
        if (LOWORD(wparam) == ExEdit::ExtendedFilter::CommandId::EXTENDEDFILTER_SELECT_DROPDOWN) {
            int type = std::clamp((int)lparam, 0, type_n - 1);
            Exdata* exdata = reinterpret_cast<Exdata*>(efp->exdata_ptr);
            if (exdata->type != type) {
                efp->exfunc->set_undo(efp->processing, 0);
                exdata->type = type;
                update_any_exdata(efp->processing, exdata_use[0].name);
                update_extendedfilter_wnd(efp);
            }
            return TRUE;
        }
    }
    return FALSE;
}

int func_window_init(HINSTANCE hinstance, HWND hwnd, int y, int base_id, int sw_param, ExEdit::Filter* efp){
    update_extendedfilter_wnd(efp);
    return 0;
}

ExEdit::Filter effect_ef = {
    .flag = ExEdit::Filter::Flag::Audio | ExEdit::Filter::Flag::Effect,
    .name = name,
    .track_n = track_n,
    .track_name = track_name,
    .track_default = track_default,
    .track_s = track_s,
    .track_e = track_e,
    .check_n = check_n,
    .check_name = check_name,
    .check_default = check_default,
    .func_proc = &func_proc,
    .func_init = &func_init,
    .func_WndProc = &func_WndProc,
    .exdata_size = exdata_size,
    .func_window_init = &func_window_init,
    .exdata_def = exdata_def,
    .exdata_use = exdata_use,
    .track_scale = track_scale,
    .track_drag_min = track_drag_min,
    .track_drag_max = track_drag_max,
};
ExEdit::Filter filter_ef = {
    .flag = ExEdit::Filter::Flag::Audio,
    .name = name,
    .track_n = track_n,
    .track_name = track_name,
    .track_default = track_default,
    .track_s = track_s,
    .track_e = track_e,
    .check_n = check_n,
    .check_name = check_name,
    .check_default = check_default,
    .func_proc = &func_proc,
    .func_init = &func_init,
    .func_WndProc = &func_WndProc,
    .exdata_size = exdata_size,
    .func_window_init = &func_window_init,
    .exdata_def = exdata_def,
    .exdata_use = exdata_use,
    .track_scale = track_scale,
    .track_drag_min = track_drag_min,
    .track_drag_max = track_drag_max,
};

ExEdit::Filter* filter_list[] = {
    &effect_ef, &filter_ef,
    NULL
};
EXTERN_C __declspec(dllexport)ExEdit::Filter** __stdcall GetFilterTableList() {
    return filter_list;
}
