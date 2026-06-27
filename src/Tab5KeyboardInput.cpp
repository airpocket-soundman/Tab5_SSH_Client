#include "Tab5KeyboardInput.hpp"

#include <M5Unified.h>

#if ENABLE_BLE_HID_KEYBOARD
#include <BLEAdvertisedDevice.h>
#include <BLEClient.h>
#include <BLEDevice.h>
#include <BLESecurity.h>
#include <BLEScan.h>
#include <esp32-hal-hosted.h>
#include <host/ble_gap.h>
#include <host/ble_gatt.h>
#include <host/ble_hs.h>
#include <host/ble_hs_adv.h>
#include <host/ble_hs_id.h>
#include <host/ble_sm.h>
#include <nimble/hci_common.h>
#include <os/os_mbuf.h>
#endif

#if USE_M5_TAB5_KEYBOARD
#include <M5UnitUnified.h>
#include <M5UnitUnifiedKEYBOARD.h>
#include <Wire.h>
#endif

#if ENABLE_USB_HOST_KEYBOARD
#include <tusb.h>
extern "C" esp_err_t init_usb_hal(bool external_phy);
#endif

namespace {
KeyboardMapper mapper;
Tab5KeyboardInput* activeInput = nullptr;

bool keyInReport(const uint8_t* previous, size_t previousCount, uint8_t keycode)
{
    for (size_t i = 0; i < previousCount; ++i) {
        if (previous[i] == keycode) {
            return true;
        }
    }
    return false;
}

#if USE_M5_TAB5_KEYBOARD
m5::unit::UnitUnified units;
m5::unit::UnitTab5Keyboard keyboard;
constexpr int8_t TAB5_KEYBOARD_SDA = 0;
constexpr int8_t TAB5_KEYBOARD_SCL = 1;
#endif

#if ENABLE_BLE_HID_KEYBOARD
constexpr uint16_t BLE_UUID_HID_SERVICE = 0x1812;
constexpr uint16_t BLE_UUID_BOOT_KEYBOARD_INPUT = 0x2A22;
constexpr uint16_t BLE_UUID_BOOT_KEYBOARD_OUTPUT = 0x2A32;
constexpr uint16_t BLE_UUID_HID_REPORT = 0x2A4D;
constexpr uint16_t BLE_UUID_PROTOCOL_MODE = 0x2A4E;
constexpr uint16_t BLE_UUID_CLIENT_CONFIG = 0x2902;
constexpr uint16_t BLE_APPEARANCE_HID_KEYBOARD = 0x03C1;
constexpr uint16_t BLE_APPEARANCE_HID_GENERIC = 0x03C0;
constexpr size_t BLE_SCAN_MAX = 8;
constexpr size_t BLE_CONN_MAX = 4;

struct BleCandidate {
    bool active{false};
    ble_addr_t addr{};
    String name;
    String address;
    uint8_t addressType{0xFF};
    int rssi{0};
    uint16_t appearance{0};
    bool connectable{false};
    bool hidService{false};
};

struct BleConnection {
    bool active{false};
    uint16_t handle{BLE_HS_CONN_HANDLE_NONE};
    String name;
    String address;
    uint8_t previousKeys[6]{};
    uint32_t reports{0};
};

struct BleKnownDevice {
    bool active{false};
    bool enabled{true};
    String name;
    String address;
    String kind{"keyboard"};
    uint8_t addressType{1};
};

BleCandidate bleCandidates[BLE_SCAN_MAX];
BleConnection bleConnections[BLE_CONN_MAX];
BleKnownDevice bleKnownDevices[BLE_CONN_MAX];
size_t bleKnownCount = 0;
size_t bleCandidateCount = 0;
uint32_t bleReports = 0;
uint32_t bleNextReconnectMs = 0;
uint32_t bleConfiguredAtMs = 0;
uint32_t bleManualScanHoldUntilMs = 0;
size_t bleReconnectIndex = 0;
bool bleInitialized = false;
bool blePairingActive = false;
bool bleAutoReconnectPaused = false;
uint8_t blePreferredOwnAddressType = 1;
uint8_t blePreferredPeerAddressType = 0xFF;
uint8_t blePreferredAuthMode = ESP_LE_AUTH_BOND;
bool blePreferredForceSecurity = true;
bool bleUseDefaultConnParams = false;
uint32_t bleGapConnectTimeoutMs = 10000;
String bleIdentityStatus = "identity not initialized";
bool bleGapTestDone = false;
bool bleGapTestConnected = false;
uint16_t bleGapTestConnHandle = BLE_HS_CONN_HANDLE_NONE;
bool bleGapHidPreferred = false;
String bleGapTestLog;
String blePendingName;
String blePendingAddress;
uint16_t bleGapScanInterval = 16;
uint16_t bleGapScanWindow = 16;
uint16_t bleGapIntervalMin = 16;
uint16_t bleGapIntervalMax = 40;
uint16_t bleGapLatency = 0;
uint16_t bleGapSupervisionTimeout = 400;

struct BleGapHidDiscovery {
    bool done{false};
    bool ok{false};
    uint16_t serviceStart{0};
    uint16_t serviceEnd{0};
    uint16_t protocolModeHandle{0};
    uint16_t bootInputHandle{0};
    uint16_t reportInputHandle{0};
    uint16_t reportInputEnd{0};
    uint16_t cccdHandle{0};
    String log;
};

BleGapHidDiscovery bleGapHid;

bool bleGapDiscDone = false;
String bleGapRawLog;

bool bleUuidEquals16(const ble_uuid_t* uuid, uint16_t value)
{
    ble_uuid16_t target = BLE_UUID16_INIT(value);
    if (!uuid) {
        return false;
    }
    if (ble_uuid_cmp(uuid, &target.u) == 0) {
        return true;
    }
    char uuidText[BLE_UUID_STR_LEN]{};
    ble_uuid_to_str(uuid, uuidText);
    char expected[8]{};
    snprintf(expected, sizeof(expected), "0x%04x", value);
    return strcmp(uuidText, expected) == 0;
}

String bleAddrToString(const ble_addr_t& addr)
{
    char text[18]{};
    snprintf(text, sizeof(text), "%02x:%02x:%02x:%02x:%02x:%02x",
             addr.val[5], addr.val[4], addr.val[3], addr.val[2], addr.val[1], addr.val[0]);
    return String(text);
}

const char* bleOwnTypeName(uint8_t type)
{
    switch (type) {
        case BLE_OWN_ADDR_PUBLIC:
            return "public";
        case BLE_OWN_ADDR_RANDOM:
            return "random-static";
        case BLE_OWN_ADDR_RPA_PUBLIC_DEFAULT:
            return "rpa-public";
        case BLE_OWN_ADDR_RPA_RANDOM_DEFAULT:
            return "rpa-random";
        default:
            return "unknown";
    }
}

String bleIdAddressText(uint8_t idType)
{
    uint8_t addr[6]{};
    int isNrpa = 0;
    int rc = ble_hs_id_copy_addr(idType, addr, &isNrpa);
    if (rc != 0) {
        return String(idType == BLE_ADDR_PUBLIC ? "public" : "random") + "=none rc=" + rc;
    }
    ble_addr_t typed{};
    typed.type = idType;
    memcpy(typed.val, addr, sizeof(typed.val));
    return String(idType == BLE_ADDR_PUBLIC ? "public" : "random") + "=" + bleAddrToString(typed) +
           (isNrpa ? "(nrpa)" : "");
}

bool ensureBleIdentityConfigured(String& error)
{
    uint8_t ownType = BLE_OWN_ADDR_RANDOM;
    int rc = ble_hs_id_infer_auto(1, &ownType);
    if (rc != 0) {
        ble_addr_t rnd{};
        int genRc = ble_hs_id_gen_rnd(0, &rnd);
        int setRc = genRc == 0 ? ble_hs_id_set_rnd(rnd.val) : genRc;
        if (setRc == 0) {
            rc = ble_hs_id_infer_auto(1, &ownType);
        }
        if (rc != 0) {
            rc = ble_hs_id_infer_auto(0, &ownType);
        }
        if (rc != 0) {
            error = String("BLE identity unavailable infer rc=") + rc + " gen/set rc=" + setRc;
            bleIdentityStatus = error;
            return false;
        }
    }
    blePreferredOwnAddressType = ownType;
    bleIdentityStatus = String("identity own=") + bleOwnTypeName(ownType) + "(" + ownType + ") " +
                        bleIdAddressText(BLE_ADDR_PUBLIC) + " " + bleIdAddressText(BLE_ADDR_RANDOM);
    error = "";
    return true;
}

bool bleStringToAddr(const String& text, ble_addr_t& addr, uint8_t addressType)
{
    unsigned values[6]{};
    if (sscanf(text.c_str(), "%02x:%02x:%02x:%02x:%02x:%02x",
               &values[5], &values[4], &values[3], &values[2], &values[1], &values[0]) != 6) {
        return false;
    }
    for (size_t i = 0; i < 6; ++i) {
        if (values[i] > 0xFF) {
            return false;
        }
        addr.val[i] = static_cast<uint8_t>(values[i]);
    }
    addr.type = addressType;
    return true;
}

String bleCleanName(const uint8_t* data, size_t len)
{
    String cleaned;
    cleaned.reserve(min<size_t>(len, 32));
    for (size_t i = 0; i < len && cleaned.length() < 32; ++i) {
        const char c = static_cast<char>(data[i]);
        if (c == '\0') {
            break;
        }
        if (c >= 0x20 && c <= 0x7e) {
            cleaned += c;
        } else if (cleaned.length()) {
            break;
        }
    }
    cleaned.trim();
    return cleaned.length() ? cleaned : "(unnamed keyboard)";
}

bool bleFieldsHaveService16(const ble_hs_adv_fields& fields, uint16_t uuid)
{
    for (uint8_t i = 0; i < fields.num_uuids16; ++i) {
        if (ble_uuid_u16(&fields.uuids16[i].u) == uuid) {
            return true;
        }
    }
    return false;
}

bool bleDiscLooksConnectable(uint8_t eventType)
{
    return eventType == BLE_HCI_ADV_RPT_EVTYPE_ADV_IND || eventType == BLE_HCI_ADV_RPT_EVTYPE_DIR_IND;
}

bool populateBleCandidateFromDisc(const ble_gap_disc_desc& desc, size_t slot)
{
    if (slot >= BLE_SCAN_MAX) {
        return false;
    }
    ble_hs_adv_fields fields{};
    if (ble_hs_adv_parse_fields(&fields, desc.data, desc.length_data) != 0) {
        return false;
    }
    const bool hidService = bleFieldsHaveService16(fields, BLE_UUID_HID_SERVICE);
    const bool keyboardAppearance = fields.appearance_is_present &&
                                    (fields.appearance == BLE_APPEARANCE_HID_KEYBOARD ||
                                     fields.appearance == BLE_APPEARANCE_HID_GENERIC);
    if (!bleDiscLooksConnectable(desc.event_type) || (!hidService && !keyboardAppearance)) {
        return false;
    }
    BleCandidate& candidate = bleCandidates[slot];
    candidate.active = true;
    candidate.addr = desc.addr;
    candidate.name = fields.name && fields.name_len ? bleCleanName(fields.name, fields.name_len) : "(unnamed keyboard)";
    candidate.address = bleAddrToString(desc.addr);
    candidate.addressType = desc.addr.type;
    candidate.rssi = desc.rssi;
    candidate.appearance = fields.appearance_is_present ? fields.appearance : 0;
    candidate.connectable = true;
    candidate.hidService = hidService;
    return true;
}

int bleGapDiscCallback(struct ble_gap_event* event, void*)
{
    if (!event) {
        return 0;
    }
    if (event->type == BLE_GAP_EVENT_DISC) {
        const ble_gap_disc_desc& desc = event->disc;
        if (bleCandidateCount < BLE_SCAN_MAX) {
            for (size_t i = 0; i < bleCandidateCount; ++i) {
                if (bleCandidates[i].address == bleAddrToString(desc.addr)) {
                    return 0;
                }
            }
            if (populateBleCandidateFromDisc(desc, bleCandidateCount)) {
                ++bleCandidateCount;
            }
        }
    } else if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        bleGapDiscDone = true;
    }
    return 0;
}

int bleGapFirstDiscCallback(struct ble_gap_event* event, void*)
{
    if (!event) {
        return 0;
    }
    if (event->type == BLE_GAP_EVENT_DISC) {
        if (bleCandidateCount == 0 && populateBleCandidateFromDisc(event->disc, 0)) {
            bleCandidateCount = 1;
        }
    } else if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        bleGapDiscDone = true;
    }
    return 0;
}

int bleGapRawDiscCallback(struct ble_gap_event* event, void*)
{
    if (!event) {
        return 0;
    }
    if (event->type == BLE_GAP_EVENT_DISC) {
        const ble_gap_disc_desc& desc = event->disc;
        if (bleGapRawLog.length() < 1800) {
            ble_hs_adv_fields fields{};
            ble_hs_adv_parse_fields(&fields, desc.data, desc.length_data);
            bleGapRawLog += String("\n  ") + bleAddrToString(desc.addr) + " type=" + desc.addr.type +
                            " rssi=" + desc.rssi + " conn=" + (bleDiscLooksConnectable(desc.event_type) ? "1" : "0") +
                            " adv=" + desc.event_type;
            if (fields.name && fields.name_len) {
                bleGapRawLog += String(" name=") + bleCleanName(fields.name, fields.name_len);
            }
            if (fields.appearance_is_present) {
                bleGapRawLog += String(" app=0x") + String(fields.appearance, HEX);
            }
            if (bleFieldsHaveService16(fields, BLE_UUID_HID_SERVICE)) {
                bleGapRawLog += " svc=0x1812";
            }
        }
    } else if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        bleGapDiscDone = true;
    }
    return 0;
}

void applyBleSecurityMode()
{
    const bool bond = (blePreferredAuthMode & ESP_LE_AUTH_BOND) != 0;
    const bool mitm = (blePreferredAuthMode & ESP_LE_AUTH_REQ_MITM) != 0;
    const bool sc = (blePreferredAuthMode & ESP_LE_AUTH_REQ_SC_ONLY) != 0;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = bond;
    ble_hs_cfg.sm_mitm = mitm;
    ble_hs_cfg.sm_sc = sc;
    ble_hs_cfg.sm_our_key_dist = bond ? (BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID) : 0;
    ble_hs_cfg.sm_their_key_dist = bond ? (BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID) : 0;
}

BleConnection* bleConnectionByHandle(uint16_t handle)
{
    for (auto& conn : bleConnections) {
        if (conn.active && conn.handle == handle) {
            return &conn;
        }
    }
    return nullptr;
}

BleConnection* bleConnectionByAddress(const String& address)
{
    for (auto& conn : bleConnections) {
        if (conn.active && conn.address == address) {
            return &conn;
        }
    }
    return nullptr;
}

BleConnection* bleAddConnection(uint16_t handle)
{
    BleConnection* freeSlot = nullptr;
    for (auto& conn : bleConnections) {
        if (conn.active && conn.handle == handle) {
            return &conn;
        }
        if (!conn.active && !freeSlot) {
            freeSlot = &conn;
        }
    }
    if (!freeSlot) {
        return nullptr;
    }
    *freeSlot = {};
    freeSlot->active = true;
    freeSlot->handle = handle;
    freeSlot->name = blePendingName;
    freeSlot->address = blePendingAddress;
    return freeSlot;
}

void bleRemoveConnection(uint16_t handle)
{
    for (auto& conn : bleConnections) {
        if (conn.active && conn.handle == handle) {
            conn = {};
            return;
        }
    }
}

size_t bleConnectedCount()
{
    size_t count = 0;
    for (const auto& conn : bleConnections) {
        if (conn.active) {
            ++count;
        }
    }
    return count;
}

void enqueueBleKeyboardReport(uint16_t connHandle, uint8_t modifier, const uint8_t* keycodes, size_t keyCount)
{
    if (!activeInput || !keycodes) {
        return;
    }
    BleConnection* conn = bleConnectionByHandle(connHandle);
    activeInput->enqueueBleReport(conn ? conn->previousKeys : nullptr, modifier, keycodes, keyCount);
    if (conn) {
        ++conn->reports;
    }
    ++bleReports;
}

int bleGapTestCallback(struct ble_gap_event* event, void*)
{
    if (!event) {
        return 0;
    }
    bleGapTestLog += String(" event=") + event->type;
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            bleGapTestLog += String(" connect.status=") + event->connect.status + " handle=" + event->connect.conn_handle;
            if (event->connect.status == 0) {
                bleGapTestConnected = true;
                bleGapTestConnHandle = event->connect.conn_handle;
                bleAddConnection(event->connect.conn_handle);
            }
            bleGapTestDone = true;
            return 0;
        case BLE_GAP_EVENT_DISCONNECT:
            bleGapTestLog += String(" disconnect.reason=") + event->disconnect.reason;
            bleGapTestDone = true;
            bleRemoveConnection(event->disconnect.conn.conn_handle);
            if (event->disconnect.conn.conn_handle == bleGapTestConnHandle) {
                bleGapTestConnected = false;
                bleGapTestConnHandle = BLE_HS_CONN_HANDLE_NONE;
            }
            return 0;
        case BLE_GAP_EVENT_NOTIFY_RX: {
            uint8_t data[16]{};
            const int len = event->notify_rx.om ? min<int>(sizeof(data), OS_MBUF_PKTLEN(event->notify_rx.om)) : 0;
            if (len > 0 && os_mbuf_copydata(event->notify_rx.om, 0, len, data) == 0) {
                bleGapTestLog += String(" notify.conn=") + event->notify_rx.conn_handle + " handle=" +
                                 event->notify_rx.attr_handle + " len=" + len;
                if (len == 8) {
                    enqueueBleKeyboardReport(event->notify_rx.conn_handle, data[0], data + 2, 6);
                } else if (len >= 9) {
                    enqueueBleKeyboardReport(event->notify_rx.conn_handle, data[1], data + 3, min<size_t>(6, len - 3));
                }
                if (activeInput) {
                    activeInput->noteBlePairStage(String("BLE GAP HID connected=") + bleConnectedCount() +
                                                  " reports=" + bleReports);
                }
            }
            return 0;
        }
        case BLE_GAP_EVENT_MTU:
            bleGapTestLog += String(" mtu=") + event->mtu.value + " handle=" + event->mtu.conn_handle;
            return 0;
        case BLE_GAP_EVENT_CONN_UPDATE:
            bleGapTestLog += String(" conn_update.status=") + event->conn_update.status;
            return 0;
        case BLE_GAP_EVENT_CONN_UPDATE_REQ:
            bleGapTestLog += " conn_update_req";
            return 0;
        case BLE_GAP_EVENT_ENC_CHANGE:
            bleGapTestLog += String(" enc.status=") + event->enc_change.status;
            return 0;
        case BLE_GAP_EVENT_PASSKEY_ACTION:
            bleGapTestLog += String(" passkey.action=") + event->passkey.params.action;
            return 0;
        default:
            return 0;
    }
}

int bleGapSvcCallback(uint16_t, const struct ble_gatt_error* error, const struct ble_gatt_svc* service, void* arg)
{
    auto* state = static_cast<BleGapHidDiscovery*>(arg);
    if (!state) {
        return 0;
    }
    if (error && error->status == 0 && service) {
        char uuidText[BLE_UUID_STR_LEN]{};
        ble_uuid_to_str(&service->uuid.u, uuidText);
        state->log += String(" svc=") + uuidText + ":" + service->start_handle + "-" + service->end_handle;
        if (bleUuidEquals16(&service->uuid.u, BLE_UUID_HID_SERVICE)) {
            state->serviceStart = service->start_handle;
            state->serviceEnd = service->end_handle;
            state->log += "(hid)";
            state->done = true;
            state->ok = true;
            return BLE_HS_EDONE;
        }
    } else if (error && error->status == BLE_HS_EDONE) {
        state->done = true;
        state->ok = state->serviceStart != 0;
        state->log += " svc.done";
    } else if (error && error->status != 0) {
        state->done = true;
        state->log += String(" svc.err=") + error->status;
    }
    return 0;
}

int bleGapListSvcCallback(uint16_t, const struct ble_gatt_error* error, const struct ble_gatt_svc* service, void* arg)
{
    auto* state = static_cast<BleGapHidDiscovery*>(arg);
    if (!state) {
        return 0;
    }
    if (error && error->status == 0 && service) {
        char uuidText[BLE_UUID_STR_LEN]{};
        ble_uuid_to_str(&service->uuid.u, uuidText);
        state->log += String(" svc=") + uuidText + ":" + service->start_handle + "-" + service->end_handle;
    } else if (error && error->status == BLE_HS_EDONE) {
        state->done = true;
        state->ok = true;
        state->log += " done";
    } else if (error && error->status != 0) {
        state->done = true;
        state->log += String(" err=") + error->status;
    }
    return 0;
}

int bleGapChrCallback(uint16_t, const struct ble_gatt_error* error, const struct ble_gatt_chr* chr, void* arg)
{
    auto* state = static_cast<BleGapHidDiscovery*>(arg);
    if (!state) {
        return 0;
    }
    if (error && error->status == 0 && chr) {
        char uuidText[BLE_UUID_STR_LEN]{};
        ble_uuid_to_str(&chr->uuid.u, uuidText);
        state->log += String(" chr=") + uuidText + ":" + chr->def_handle + "/" + chr->val_handle +
                      " prop=0x" + String(chr->properties, HEX);
        if (bleUuidEquals16(&chr->uuid.u, BLE_UUID_PROTOCOL_MODE)) {
            state->protocolModeHandle = chr->val_handle;
            state->log += String(" proto=") + chr->val_handle;
        } else if (bleUuidEquals16(&chr->uuid.u, BLE_UUID_BOOT_KEYBOARD_INPUT)) {
            state->bootInputHandle = chr->val_handle;
            state->log += String(" bootin=") + chr->val_handle;
        } else if ((chr->properties & BLE_GATT_CHR_F_NOTIFY) &&
                   bleUuidEquals16(&chr->uuid.u, BLE_UUID_HID_REPORT)) {
            state->reportInputHandle = chr->val_handle;
            state->log += String(" report=") + chr->val_handle;
        }
    } else if (error && error->status == BLE_HS_EDONE) {
        state->done = true;
        state->ok = state->bootInputHandle != 0 || state->reportInputHandle != 0;
        state->log += " chr.done";
    } else if (error && error->status != 0) {
        state->done = true;
        state->log += String(" chr.err=") + error->status;
    }
    return 0;
}

int bleGapDscCallback(uint16_t, const struct ble_gatt_error* error, uint16_t, const struct ble_gatt_dsc* dsc, void* arg)
{
    auto* state = static_cast<BleGapHidDiscovery*>(arg);
    if (!state) {
        return 0;
    }
    if (error && error->status == 0 && dsc && bleUuidEquals16(&dsc->uuid.u, BLE_UUID_CLIENT_CONFIG)) {
        char uuidText[BLE_UUID_STR_LEN]{};
        ble_uuid_to_str(&dsc->uuid.u, uuidText);
        state->log += String(" dsc=") + uuidText + ":" + dsc->handle;
        state->cccdHandle = dsc->handle;
        state->log += String(" cccd=") + dsc->handle;
    } else if (error && error->status == 0 && dsc) {
        char uuidText[BLE_UUID_STR_LEN]{};
        ble_uuid_to_str(&dsc->uuid.u, uuidText);
        state->log += String(" dsc=") + uuidText + ":" + dsc->handle;
    } else if (error && error->status == BLE_HS_EDONE) {
        state->done = true;
        state->ok = state->cccdHandle != 0;
        state->log += " dsc.done";
    } else if (error && error->status != 0) {
        state->done = true;
        state->log += String(" dsc.err=") + error->status;
    }
    return 0;
}

bool ensureBleInitialized(String& error)
{
    if (bleInitialized) {
        applyBleSecurityMode();
        return true;
    }
    BLEDevice::init("Tab5sh");
    applyBleSecurityMode();
    if (!ensureBleIdentityConfigured(error)) {
        return false;
    }
    bleInitialized = true;
    error = "";
    return true;
}

bool bleHostedReadyForAutoReconnect()
{
    if (hostedIsInitialized()) {
        return true;
    }
    return static_cast<int32_t>(millis() - bleConfiguredAtMs) >= 15000;
}

void fillBleGapParams(ble_gap_conn_params& params)
{
    params = {};
    params.scan_itvl = bleGapScanInterval;
    params.scan_window = bleGapScanWindow;
    params.itvl_min = bleGapIntervalMin;
    params.itvl_max = bleGapIntervalMax;
    params.latency = bleGapLatency;
    params.supervision_timeout = bleGapSupervisionTimeout;
    params.min_ce_len = 0;
    params.max_ce_len = 0;
}

const ble_gap_conn_params* bleCurrentConnParams(ble_gap_conn_params& params)
{
    if (bleUseDefaultConnParams) {
        return nullptr;
    }
    fillBleGapParams(params);
    return &params;
}
#endif

#if ENABLE_USB_HOST_KEYBOARD
bool usbHostStarted = false;
uint32_t usbReports = 0;
uint8_t usbKeyboardCount = 0;

struct UsbKeyboardState {
    bool active{false};
    uint8_t devAddr{0};
    uint8_t instance{0};
    uint8_t previous[6]{};
};

UsbKeyboardState usbStates[6];

UsbKeyboardState* usbState(uint8_t devAddr, uint8_t instance, bool create)
{
    UsbKeyboardState* freeSlot = nullptr;
    for (auto& state : usbStates) {
        if (state.active && state.devAddr == devAddr && state.instance == instance) {
            return &state;
        }
        if (!state.active && !freeSlot) {
            freeSlot = &state;
        }
    }
    if (!create || !freeSlot) {
        return nullptr;
    }
    *freeSlot = {};
    freeSlot->active = true;
    freeSlot->devAddr = devAddr;
    freeSlot->instance = instance;
    return freeSlot;
}

void clearUsbState(uint8_t devAddr, uint8_t instance)
{
    for (auto& state : usbStates) {
        if (state.active && state.devAddr == devAddr && state.instance == instance) {
            state = {};
            return;
        }
    }
}

bool keyWasPressed(const UsbKeyboardState& state, uint8_t keycode)
{
    return keyInReport(state.previous, sizeof(state.previous), keycode);
}

bool beginUsbHost()
{
    if (usbHostStarted) {
        return true;
    }
    init_usb_hal(false);
    tusb_rhport_init_t init = {};
    init.role = TUSB_ROLE_HOST;
#if CONFIG_IDF_TARGET_ESP32P4
    init.speed = TUSB_SPEED_HIGH;
    usbHostStarted = tusb_init(BOARD_TUH_RHPORT, &init);
#else
    init.speed = TUSB_SPEED_FULL;
    usbHostStarted = tusb_init(0, &init);
#endif
    return usbHostStarted;
}
#endif

KeyAction mapNamedKey(const char* chars)
{
    String token(chars);
    token.toLowerCase();
    token.replace("_", " ");
    token.replace("-", " ");
    if (token == "backspace" || token == "bs") {
        return mapper.mapChar(static_cast<char>(0x7F));
    }
    if (token == "enter" || token == "return") {
        return mapper.mapChar('\r');
    }
    if (token == "tab") {
        return mapper.mapChar('\t');
    }
    if (token == "esc" || token == "escape") {
        return mapper.mapChar(static_cast<char>(0x1B));
    }
    if (token == "space") {
        return mapper.mapChar(' ');
    }
    if (token == "minus" || token == "hyphen") {
        return mapper.mapChar('-');
    }
    if (token == "underscore") {
        return mapper.mapChar('_');
    }
    if (token == "equal" || token == "equals") {
        return mapper.mapChar('=');
    }
    if (token == "plus") {
        return mapper.mapChar('+');
    }
    if (token == "left bracket" || token == "open bracket" || token == "lbracket") {
        return mapper.mapChar('[');
    }
    if (token == "right bracket" || token == "close bracket" || token == "rbracket") {
        return mapper.mapChar(']');
    }
    if (token == "left brace" || token == "open brace" || token == "lbrace") {
        return mapper.mapChar('{');
    }
    if (token == "right brace" || token == "close brace" || token == "rbrace") {
        return mapper.mapChar('}');
    }
    if (token == "backslash") {
        return mapper.mapChar('\\');
    }
    if (token == "pipe" || token == "vertical bar") {
        return mapper.mapChar('|');
    }
    if (token == "semicolon") {
        return mapper.mapChar(';');
    }
    if (token == "colon") {
        return mapper.mapChar(':');
    }
    if (token == "quote" || token == "apostrophe" || token == "single quote") {
        return mapper.mapChar('\'');
    }
    if (token == "double quote" || token == "quotation mark") {
        return mapper.mapChar('"');
    }
    if (token == "grave" || token == "backquote" || token == "backtick") {
        return mapper.mapChar('`');
    }
    if (token == "tilde") {
        return mapper.mapChar('~');
    }
    if (token == "comma") {
        return mapper.mapChar(',');
    }
    if (token == "period" || token == "dot") {
        return mapper.mapChar('.');
    }
    if (token == "slash" || token == "forward slash") {
        return mapper.mapChar('/');
    }
    if (token == "question" || token == "question mark") {
        return mapper.mapChar('?');
    }
    if (token == "exclamation" || token == "exclamation mark") {
        return mapper.mapChar('!');
    }
    if (token == "at" || token == "at sign") {
        return mapper.mapChar('@');
    }
    if (token == "hash" || token == "number sign" || token == "pound") {
        return mapper.mapChar('#');
    }
    if (token == "dollar" || token == "dollar sign") {
        return mapper.mapChar('$');
    }
    if (token == "percent" || token == "percent sign") {
        return mapper.mapChar('%');
    }
    if (token == "caret" || token == "circumflex") {
        return mapper.mapChar('^');
    }
    if (token == "ampersand") {
        return mapper.mapChar('&');
    }
    if (token == "asterisk" || token == "star") {
        return mapper.mapChar('*');
    }
    if (token == "left paren" || token == "open paren" || token == "left parenthesis") {
        return mapper.mapChar('(');
    }
    if (token == "right paren" || token == "close paren" || token == "right parenthesis") {
        return mapper.mapChar(')');
    }
    if (token == "less" || token == "less than") {
        return mapper.mapChar('<');
    }
    if (token == "greater" || token == "greater than") {
        return mapper.mapChar('>');
    }
    if (token == "up" || token == "up arrow" || token == "arrow up") {
        return {KeyActionType::Text, "\x1B[A", 0};
    }
    if (token == "down" || token == "down arrow" || token == "arrow down") {
        return {KeyActionType::Text, "\x1B[B", 0};
    }
    if (token == "right" || token == "right arrow" || token == "arrow right") {
        return {KeyActionType::Text, "\x1B[C", 0};
    }
    if (token == "left" || token == "left arrow" || token == "arrow left") {
        return {KeyActionType::Text, "\x1B[D", 0};
    }
    if (token == "home") {
        return {KeyActionType::Text, "\x1B[H", 0};
    }
    if (token == "end") {
        return {KeyActionType::Text, "\x1B[F", 0};
    }
    if (token == "delete" || token == "del") {
        return {KeyActionType::Text, "\x1B[3~", 0};
    }
    if (token == "insert" || token == "ins") {
        return {KeyActionType::Text, "\x1B[2~", 0};
    }
    if (token == "page up" || token == "pgup") {
        return {KeyActionType::Text, "\x1B[5~", 0};
    }
    if (token == "page down" || token == "pgdn") {
        return {KeyActionType::Text, "\x1B[6~", 0};
    }
    return {};
}

KeyAction mapCtrlCharacter(char c)
{
    if (c >= 'a' && c <= 'z') {
        return {KeyActionType::Text, String(static_cast<char>(c - 'a' + 1)), 0};
    }
    if (c >= 'A' && c <= 'Z') {
        return {KeyActionType::Text, String(static_cast<char>(c - 'A' + 1)), 0};
    }
    switch (c) {
        case '[':
            return {KeyActionType::Text, String(static_cast<char>(0x1B)), 0};
        case '\\':
            return {KeyActionType::Text, String(static_cast<char>(0x1C)), 0};
        case ']':
            return {KeyActionType::Text, String(static_cast<char>(0x1D)), 0};
        case '^':
            return {KeyActionType::Text, String(static_cast<char>(0x1E)), 0};
        case '_':
            return {KeyActionType::Text, String(static_cast<char>(0x1F)), 0};
        case '?':
            return {KeyActionType::Text, String(static_cast<char>(0x7F)), 0};
        default:
            return {};
    }
}

#if ENABLE_USB_HOST_KEYBOARD
extern "C" void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t idx, const uint8_t*, uint16_t)
{
    if (tuh_hid_interface_protocol(dev_addr, idx) == HID_ITF_PROTOCOL_KEYBOARD) {
        usbState(dev_addr, idx, true);
        ++usbKeyboardCount;
        if (activeInput) {
            activeInput->noteUsbKeyboardMounted();
        }
        tuh_hid_receive_report(dev_addr, idx);
    }
}

extern "C" void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t idx)
{
    if (tuh_hid_interface_protocol(dev_addr, idx) == HID_ITF_PROTOCOL_KEYBOARD) {
        clearUsbState(dev_addr, idx);
        if (usbKeyboardCount) {
            --usbKeyboardCount;
        }
        if (activeInput) {
            activeInput->noteUsbKeyboardUnmounted();
        }
    }
}

extern "C" void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t idx, const uint8_t* report, uint16_t len)
{
    if (tuh_hid_interface_protocol(dev_addr, idx) == HID_ITF_PROTOCOL_KEYBOARD && len >= sizeof(hid_keyboard_report_t)) {
        const auto* keyboardReport = reinterpret_cast<const hid_keyboard_report_t*>(report);
        if (activeInput) {
            activeInput->enqueueUsbReport(dev_addr, idx, keyboardReport->modifier, keyboardReport->keycode,
                                          sizeof(keyboardReport->keycode));
        }
        ++usbReports;
    }
    tuh_hid_receive_report(dev_addr, idx);
}
#endif
}

void Tab5KeyboardInput::configure(const KeyboardConfig& config)
{
    mapper.configure(config);
    _bleEnabled = config.bleKeyboardEnabled;
#if ENABLE_BLE_HID_KEYBOARD
    bleKnownCount = 0;
    for (const auto& profile : config.bleDevices) {
        if (bleKnownCount >= BLE_CONN_MAX || !profile.address.length()) {
            continue;
        }
        BleKnownDevice* target = nullptr;
        for (size_t i = 0; i < bleKnownCount; ++i) {
            if (bleKnownDevices[i].address == profile.address ||
                (profile.name.length() && bleKnownDevices[i].name == profile.name &&
                 bleKnownDevices[i].kind == (profile.kind.length() ? profile.kind : "keyboard"))) {
                target = &bleKnownDevices[i];
                break;
            }
        }
        if (!target) {
            target = &bleKnownDevices[bleKnownCount++];
        }
        auto& known = *target;
        known = {};
        known.active = true;
        known.enabled = profile.enabled;
        known.name = profile.name.length() ? profile.name : "(paired HID)";
        known.address = profile.address;
        known.kind = profile.kind.length() ? profile.kind : "keyboard";
        known.addressType = profile.addressType <= 3 ? profile.addressType : 1;
    }
    _bleName = bleKnownCount ? bleKnownDevices[min(config.activeBle, bleKnownCount - 1)].name : "";
    _bleAddress = bleKnownCount ? bleKnownDevices[min(config.activeBle, bleKnownCount - 1)].address : "";
    _bleAddressType = bleKnownCount ? bleKnownDevices[min(config.activeBle, bleKnownCount - 1)].addressType : 1;
    _bleKind = bleKnownCount ? bleKnownDevices[min(config.activeBle, bleKnownCount - 1)].kind : "keyboard";
    bleReconnectIndex = bleKnownCount ? min(config.activeBle, bleKnownCount - 1) : 0;
#else
    _bleName = "";
    _bleAddress = "";
    _bleAddressType = 1;
    _bleKind = "keyboard";
#endif
    if (_bleEnabled) {
        _bleRuntimeStatus = bleKnownCount ? String("BLE HID enabled; reconnect pending devices=") + bleKnownCount
                                          : "BLE HID enabled; no paired device";
        bleConfiguredAtMs = millis();
        bleNextReconnectMs = millis() + 500;
    } else {
        _bleRuntimeStatus = "BLE HID disabled";
    }
    activeInput = this;
}

bool Tab5KeyboardInput::begin()
{
    activeInput = this;
    bool tab5Ready = false;
#if USE_M5_TAB5_KEYBOARD
    auto cfg = keyboard.config();
    cfg.mode = m5::unit::tab5_keyboard::Mode::Character;
    cfg.software_repeat = true;
    cfg.irq_pin = -1;
    cfg.interval_ms = 10;
    keyboard.config(cfg);

    Wire.end();
    Wire.begin(TAB5_KEYBOARD_SDA, TAB5_KEYBOARD_SCL, keyboard.component_config().clock);
    if (!units.add(keyboard, Wire) || !units.begin()) {
        _status = "Tab5 keyboard not found; Serial input fallback active";
    } else {
        uint8_t fw = keyboard.firmwareVersion();
        keyboard.writeMode(m5::unit::tab5_keyboard::Mode::Character);
        keyboard.writeRgbMode(m5::unit::tab5_keyboard::RgbMode::Custom);
        keyboard.writeRgb(0, 0, 0, 0);
        keyboard.writeRgb(1, 0, 0, 0);
        _status = String("Tab5 keyboard ready fw=0x") + String(fw, HEX);
        tab5Ready = true;
    }
#else
    _status = "Serial input fallback active";
#endif
#if ENABLE_USB_HOST_KEYBOARD
    if (beginUsbHost()) {
        _status += "; USB host ready";
    } else {
        _status += "; USB host unavailable";
    }
#endif
    if (_bleEnabled) {
        _status += "; BLE keyboard pending";
    }
    return tab5Ready;
}

void Tab5KeyboardInput::update()
{
#if ENABLE_USB_HOST_KEYBOARD
    if (usbHostStarted) {
        tuh_task_ext(0, false);
    }
#endif
#if USE_M5_TAB5_KEYBOARD
    keyboard.update(true);
    while (!keyboard.empty()) {
        auto event = keyboard.oldest();
        ++_events;
        if (event.type == m5::unit::tab5_keyboard::EventType::Character) {
            KeyAction named = mapNamedKey(event.chr.chars);
            if (named.type != KeyActionType::None) {
                push(named);
            } else {
                for (uint8_t i = 0; i < event.chr.length; ++i) {
                    KeyAction action = event.isCtrl() ? mapCtrlCharacter(event.chr.chars[i]) : KeyAction{};
                    push(action.type != KeyActionType::None ? action : mapper.mapChar(event.chr.chars[i]));
                }
            }
        } else if (event.type == m5::unit::tab5_keyboard::EventType::Hid) {
            push(mapper.mapHid(event.modifier, event.hid.keycode));
        }
        _status = String("Tab5 keyboard events=") + _events;
        keyboard.discard();
    }
#endif

    while (Serial.available()) {
        push(mapper.mapChar(static_cast<char>(Serial.read())));
    }

#if ENABLE_BLE_HID_KEYBOARD
    const size_t connectedBleCount = bleConnectedCount();
    if (connectedBleCount > 0) {
        _bleRuntimeStatus = String("BLE GAP HID connected=") + connectedBleCount + " reports=" + bleReports;
    }
    if (connectedBleCount == 0 &&
        !blePairingActive && _bleEnabled && bleKnownCount > 0 &&
        !bleAutoReconnectPaused &&
        static_cast<int32_t>(millis() - bleManualScanHoldUntilMs) >= 0 &&
        static_cast<int32_t>(millis() - bleNextReconnectMs) >= 0) {
        if (!bleHostedReadyForAutoReconnect()) {
            bleNextReconnectMs = millis() + 500;
            _bleRuntimeStatus = "BLE HID waiting for hosted link";
            return;
        }
        bleNextReconnectMs = millis() + 5000;
        BleKnownDevice* target = nullptr;
        for (size_t i = 0; i < bleKnownCount; ++i) {
            size_t index = (bleReconnectIndex + i) % bleKnownCount;
            if (!bleKnownDevices[index].enabled || !bleKnownDevices[index].address.length()) {
                continue;
            }
            if (bleConnectionByAddress(bleKnownDevices[index].address)) {
                continue;
            }
            target = &bleKnownDevices[index];
            bleReconnectIndex = (index + 1) % bleKnownCount;
            break;
        }
        if (!target) {
            return;
        }
        bleCandidateCount = 1;
        bleCandidates[0] = {};
        bleCandidates[0].active = bleStringToAddr(target->address, bleCandidates[0].addr, target->addressType);
        bleCandidates[0].address = target->address;
        bleCandidates[0].addressType = bleCandidates[0].addr.type;
        bleCandidates[0].name = target->name;
        String connectResult;
        const bool savedDefaultParams = bleUseDefaultConnParams;
        const uint32_t savedConnectTimeoutMs = bleGapConnectTimeoutMs;
        bleUseDefaultConnParams = true;
        bleGapConnectTimeoutMs = 1200;
        if (bleCandidates[0].active && bleGapTest(0, connectResult)) {
            String secureResult;
            bleGapSecure(secureResult);
            String hidResult;
            if (bleGapSubscribeHid(hidResult)) {
                bleGapHidPreferred = true;
                _bleName = target->name;
                if (!_bleAddress.length()) {
                    _bleAddress = target->address;
                    _bleAddressType = target->addressType;
                }
                _bleKind = target->kind;
                _bleRuntimeStatus = String("BLE GAP HID connected=") + bleConnectedCount() + " reports=" + bleReports;
                _status = String("BLE HID connected ") + target->name;
            } else {
                _bleRuntimeStatus = String("BLE reconnect HID failed: ") + hidResult;
            }
        } else {
            _bleRuntimeStatus = String("BLE reconnect failed: ") + connectResult;
        }
        bleUseDefaultConnParams = savedDefaultParams;
        bleGapConnectTimeoutMs = savedConnectTimeoutMs;
    }
#endif
}

void Tab5KeyboardInput::noteUsbKeyboardMounted()
{
    _status = String("USB keyboard ready count=") + usbKeyboardCount;
}

void Tab5KeyboardInput::noteUsbKeyboardUnmounted()
{
    _status = String("USB keyboard removed count=") + usbKeyboardCount;
}

void Tab5KeyboardInput::noteBleKeyboardDisconnected()
{
    _bleRuntimeStatus = "BLE keyboard disconnected; reconnect pending";
    _status = _bleRuntimeStatus;
}

void Tab5KeyboardInput::noteBlePairStage(const String& status)
{
    _bleRuntimeStatus = status;
    _status = status;
}

void Tab5KeyboardInput::enqueueUsbReport(uint8_t devAddr, uint8_t instance, uint8_t modifier, const uint8_t* keycodes,
                                         size_t keyCount)
{
#if ENABLE_USB_HOST_KEYBOARD
    UsbKeyboardState* state = usbState(devAddr, instance, true);
    if (!state) {
        return;
    }
    for (size_t i = 0; i < keyCount; ++i) {
        uint8_t keycode = keycodes[i];
        if (!keycode || keyWasPressed(*state, keycode)) {
            continue;
        }
        push(mapper.mapHid(modifier, keycode));
    }
    memset(state->previous, 0, sizeof(state->previous));
    memcpy(state->previous, keycodes, min(keyCount, sizeof(state->previous)));
    _status = String("USB keyboard reports=") + usbReports;
#else
    (void)devAddr;
    (void)instance;
    (void)modifier;
    (void)keycodes;
    (void)keyCount;
#endif
}

void Tab5KeyboardInput::enqueueBleReport(uint8_t* previousKeys, uint8_t modifier, const uint8_t* keycodes, size_t keyCount)
{
#if ENABLE_BLE_HID_KEYBOARD
    uint8_t fallbackPrevious[6]{};
    if (!previousKeys) {
        previousKeys = fallbackPrevious;
    }
    for (size_t i = 0; i < keyCount; ++i) {
        const uint8_t keycode = keycodes[i];
        if (!keycode || keyInReport(previousKeys, 6, keycode)) {
            continue;
        }
        push(mapper.mapHid(modifier, keycode));
    }
    memset(previousKeys, 0, 6);
    memcpy(previousKeys, keycodes, min<size_t>(keyCount, 6));
    _status = String("BLE keyboard reports=") + bleReports;
#else
    (void)previousKeys;
    (void)modifier;
    (void)keycodes;
    (void)keyCount;
#endif
}

bool Tab5KeyboardInput::available() const
{
    return _head != _tail;
}

KeyAction Tab5KeyboardInput::read()
{
    if (!available()) {
        return {};
    }
    KeyAction action = _queue[_tail];
    _tail = (_tail + 1) % QueueSize;
    return action;
}

String Tab5KeyboardInput::bleStatus() const
{
    String text = _bleRuntimeStatus;
#if ENABLE_BLE_HID_KEYBOARD
    if (bleGapTestConnected && bleGapTestConnHandle != BLE_HS_CONN_HANDLE_NONE) {
        text = String("BLE GAP HID connected; reports=") + bleReports;
    }
    if (bleIdentityStatus.length()) {
        text += String(" ") + bleIdentityStatus;
    }
#endif
    if (_bleName.length()) {
        text += String(" name=") + _bleName;
    }
    if (_bleAddress.length()) {
        text += String(" addr=") + _bleAddress;
    }
    return text;
}

size_t Tab5KeyboardInput::bleScanCount() const
{
#if ENABLE_BLE_HID_KEYBOARD
    return bleCandidateCount;
#else
    return 0;
#endif
}

String Tab5KeyboardInput::bleDevicesStatus() const
{
#if ENABLE_BLE_HID_KEYBOARD
    String result = String("known=") + bleKnownCount + " connected=" + bleConnectedCount();
    for (size_t i = 0; i < bleKnownCount; ++i) {
        const auto& known = bleKnownDevices[i];
        result += String("\n  ") + i + ": " + (known.enabled ? "on " : "off ") + known.kind + " " +
                  known.name + " " + known.address + " type=" + known.addressType;
        if (const BleConnection* conn = bleConnectionByAddress(known.address)) {
            result += String(" connected handle=") + conn->handle + " reports=" + conn->reports;
        }
    }
    for (const auto& conn : bleConnections) {
        if (!conn.active || bleKnownCount) {
            bool listed = false;
            for (size_t i = 0; i < bleKnownCount; ++i) {
                if (bleKnownDevices[i].address == conn.address) {
                    listed = true;
                    break;
                }
            }
            if (listed) {
                continue;
            }
        }
        if (conn.active) {
            result += String("\n  ?: connected ") + conn.name + " " + conn.address +
                      " handle=" + conn.handle + " reports=" + conn.reports;
        }
    }
    return result;
#else
    return "BLE HID backend unavailable on ESP32-P4 Arduino build";
#endif
}

String Tab5KeyboardInput::bleScanEntry(size_t index) const
{
#if ENABLE_BLE_HID_KEYBOARD
    if (index >= bleCandidateCount || !bleCandidates[index].active) {
        return "";
    }
    const BleCandidate& candidate = bleCandidates[index];
    return String(index) + ": " + candidate.name + " " + candidate.address + " type=" + candidate.addressType +
           " rssi=" + candidate.rssi;
#else
    (void)index;
    return "";
#endif
}

bool Tab5KeyboardInput::bleScan(String& result)
{
#if ENABLE_BLE_HID_KEYBOARD
    if (!_bleEnabled) {
        result = "BLE keyboard is disabled; run ble enable first";
        return false;
    }
    String error;
    if (!ensureBleInitialized(error)) {
        result = error;
        return false;
    }
    const bool savedAutoReconnectPaused = bleAutoReconnectPaused;
    bleAutoReconnectPaused = true;
    bleManualScanHoldUntilMs = millis() + 60000;
    bleCandidateCount = 0;
    for (auto& candidate : bleCandidates) {
        candidate = {};
    }
    ble_gap_disc_params params{};
    params.itvl = 80;
    params.window = 60;
    params.passive = 0;
    params.filter_duplicates = 1;
    params.limited = 0;
    bleGapDiscDone = false;
    int rc = ble_gap_disc(blePreferredOwnAddressType, 5000, &params, bleGapDiscCallback, nullptr);
    if (rc != 0) {
        result = String("scan start rc=") + rc;
        _bleRuntimeStatus = result;
        bleAutoReconnectPaused = savedAutoReconnectPaused;
        return false;
    }
    uint32_t start = millis();
    while (!bleGapDiscDone && millis() - start < 5500) {
        delay(20);
    }
    ble_gap_disc_cancel();
    if (!bleCandidateCount) {
        result = "no pairable BLE HID keyboards found";
        _bleRuntimeStatus = result;
        bleAutoReconnectPaused = savedAutoReconnectPaused;
        return false;
    }
    result = String("found ") + bleCandidateCount + " pairable keyboard(s)";
    for (size_t i = 0; i < bleCandidateCount; ++i) {
        const BleCandidate& candidate = bleCandidates[i];
        result += String("\n  ") + i + ": " + candidate.name + " " + candidate.address + " type=" +
                  candidate.addressType + " rssi=" + candidate.rssi;
        if (candidate.appearance) {
            result += String(" app=0x") + String(candidate.appearance, HEX);
        }
    }
    _bleRuntimeStatus = result;
    bleAutoReconnectPaused = savedAutoReconnectPaused;
    return true;
#else
    result = "BLE HID backend unavailable on ESP32-P4 Arduino build";
    return false;
#endif
}

bool Tab5KeyboardInput::bleScanRaw(String& result)
{
#if ENABLE_BLE_HID_KEYBOARD
    String error;
    if (!ensureBleInitialized(error)) {
        result = error;
        return false;
    }
    const bool savedAutoReconnectPaused = bleAutoReconnectPaused;
    bleAutoReconnectPaused = true;
    ble_gap_disc_params params{};
    params.itvl = 80;
    params.window = 60;
    params.passive = 0;
    params.filter_duplicates = 0;
    params.limited = 0;
    bleGapDiscDone = false;
    bleGapRawLog = "";
    int rc = ble_gap_disc(blePreferredOwnAddressType, 5000, &params, bleGapRawDiscCallback, nullptr);
    if (rc != 0) {
        result = String("raw scan start rc=") + rc;
        bleAutoReconnectPaused = savedAutoReconnectPaused;
        return false;
    }
    uint32_t start = millis();
    while (!bleGapDiscDone && millis() - start < 5500) {
        delay(20);
    }
    ble_gap_disc_cancel();
    result = bleGapRawLog.length() ? String("raw found:") + bleGapRawLog : "raw found no devices";
    bleAutoReconnectPaused = savedAutoReconnectPaused;
    return bleGapRawLog.length() > 0;
#else
    result = "BLE HID backend unavailable on ESP32-P4 Arduino build";
    return false;
#endif
}

bool Tab5KeyboardInput::blePair(size_t index, String& result)
{
#if ENABLE_BLE_HID_KEYBOARD
    if (!_bleEnabled) {
        result = "BLE keyboard is disabled; run ble enable first";
        return false;
    }
    if (index >= bleCandidateCount || !bleCandidates[index].active) {
        result = "invalid scan index; run ble scan first";
        return false;
    }
    blePairingActive = true;
    bleAutoReconnectPaused = true;
    bleManualScanHoldUntilMs = 0;
    _bleAddress = "";
    _bleAddressType = bleCandidates[index].addressType;
    String connectResult;
    bool ok = bleGapTest(index, connectResult);
    String secureResult;
    if (ok) {
        bleGapSecure(secureResult);
    }
    String hidResult;
    if (ok) {
        ok = bleGapSubscribeHid(hidResult);
    }
    BleCandidate& candidate = bleCandidates[index];
    if (!ok) {
        blePairingActive = false;
        bleAutoReconnectPaused = false;
        _bleRuntimeStatus = String("BLE pair failed: ") + connectResult + "; " + secureResult + "; " + hidResult;
        result = _bleRuntimeStatus;
        return false;
    }
    blePairingActive = false;
    bleAutoReconnectPaused = false;
    _bleName = candidate.name;
    if (!_bleAddress.length()) {
        _bleAddress = candidate.address;
        _bleAddressType = candidate.addressType;
    }
    _bleKind = "keyboard";
    bleGapHidPreferred = true;
    _bleRuntimeStatus = String("BLE GAP HID connected; reports=") + bleReports;
    result = candidate.name + " " + candidate.address + "; " + connectResult + "; " + secureResult + "; " + hidResult;
    return true;
#else
    (void)index;
    result = "BLE HID backend unavailable on ESP32-P4 Arduino build";
    return false;
#endif
}

bool Tab5KeyboardInput::bleScanAndPairFirst(String& result)
{
#if ENABLE_BLE_HID_KEYBOARD
    if (!_bleEnabled) {
        result = "BLE keyboard is disabled; run ble enable first";
        return false;
    }
    String error;
    if (!ensureBleInitialized(error)) {
        result = error;
        return false;
    }
    bleAutoReconnectPaused = true;
    if (!bleScan(result)) {
        bleAutoReconnectPaused = false;
        result = "no pairable BLE HID keyboards found";
        _bleRuntimeStatus = result;
        return false;
    }

    bool ok = blePair(0, result);
    bleAutoReconnectPaused = false;
    return ok;
#else
    result = "BLE HID backend unavailable on ESP32-P4 Arduino build";
    return false;
#endif
}

bool Tab5KeyboardInput::bleGapTest(size_t index, String& result)
{
#if ENABLE_BLE_HID_KEYBOARD
    if (index >= bleCandidateCount || !bleCandidates[index].active) {
        result = "invalid scan index; run ble scan first";
        return false;
    }
    String error;
    if (!ensureBleInitialized(error)) {
        result = error;
        return false;
    }
    BleCandidate& candidate = bleCandidates[index];
    if (BleConnection* existing = bleConnectionByAddress(candidate.address)) {
        bleGapTestConnected = true;
        bleGapTestConnHandle = existing->handle;
        result = String("gap already connected addr=") + candidate.address + " handle=" + existing->handle;
        return true;
    }
    const uint8_t peerType = blePreferredPeerAddressType <= 3 ? blePreferredPeerAddressType : candidate.addressType;
    ble_addr_t peer = candidate.addr;
    peer.type = peerType;

    ble_gap_conn_params params;
    const ble_gap_conn_params* connParams = bleCurrentConnParams(params);

    bleGapTestDone = false;
    bleGapTestConnected = false;
    bleGapTestConnHandle = BLE_HS_CONN_HANDLE_NONE;
    bleGapTestLog = "";
    blePendingName = candidate.name;
    blePendingAddress = candidate.address;
    applyBleSecurityMode();
    int rc = ble_gap_connect(blePreferredOwnAddressType, &peer, static_cast<int32_t>(bleGapConnectTimeoutMs),
                             connParams, bleGapTestCallback, nullptr);
    if (rc != 0) {
        result = String("gap_connect rc=") + rc + " addr=" + candidate.address + " own=" + blePreferredOwnAddressType +
                 " peer=" + peerType + (connParams ? " params=custom" : " params=default");
        return false;
    }
    const uint32_t start = millis();
    while (!bleGapTestDone && millis() - start < bleGapConnectTimeoutMs + 2000) {
        delay(20);
    }
    if (!bleGapTestDone) {
        ble_gap_conn_cancel();
        result = String("gap timeout no event addr=") + candidate.address + " own=" + blePreferredOwnAddressType +
                 " peer=" + peerType + (connParams ? " params=custom" : " params=default") + bleGapTestLog;
        return false;
    }
    result = String("gap done connected=") + (bleGapTestConnected ? "1" : "0") + " addr=" + candidate.address +
             " own=" + blePreferredOwnAddressType + " peer=" + peerType +
             (connParams ? " params=custom" : " params=default") + bleGapTestLog;
    return bleGapTestConnected;
#else
    (void)index;
    result = "BLE HID backend unavailable on ESP32-P4 Arduino build";
    return false;
#endif
}

bool Tab5KeyboardInput::bleGapScanAndTest(String& result)
{
#if ENABLE_BLE_HID_KEYBOARD
    if (!_bleEnabled) {
        result = "BLE keyboard is disabled; run ble enable first";
        return false;
    }
    String error;
    if (!ensureBleInitialized(error)) {
        result = error;
        return false;
    }
    bleAutoReconnectPaused = true;
    blePairingActive = true;
    bleCandidateCount = 0;
    for (auto& candidate : bleCandidates) {
        candidate = {};
    }
    struct GapAutoAttempt {
        uint8_t ownType;
        uint8_t peerType;
        uint16_t intervalMin;
        uint16_t intervalMax;
        uint16_t supervisionTimeout;
        bool defaultParams;
        const char* label;
    };
    const GapAutoAttempt attempts[] = {
        {blePreferredOwnAddressType, 0xFF, 16, 40, 400, true,  "identity-rpa-default"},
        {BLE_OWN_ADDR_RPA_RANDOM_DEFAULT, 0xFF, 16, 40, 400, true,  "rpa-random-default"},
        {BLE_OWN_ADDR_RPA_PUBLIC_DEFAULT, 0xFF, 16, 40, 400, true,  "rpa-public-default"},
        {BLE_OWN_ADDR_RANDOM, 0xFF, 16, 40, 400, true,  "random-default"},
        {BLE_OWN_ADDR_RANDOM, 1,    16, 40, 400, false, "random-random-fast"},
        {BLE_OWN_ADDR_RPA_RANDOM_DEFAULT, 1, 24, 48, 600, false, "rpa-random-random-mid"},
        {BLE_OWN_ADDR_PUBLIC, 0xFF, 24, 48, 600, true,  "public-default"},
    };
    const uint8_t savedOwnType = blePreferredOwnAddressType;
    const uint8_t savedPeerType = blePreferredPeerAddressType;
    const bool savedDefaultParams = bleUseDefaultConnParams;
    const uint16_t savedIntervalMin = bleGapIntervalMin;
    const uint16_t savedIntervalMax = bleGapIntervalMax;
    const uint16_t savedSupervisionTimeout = bleGapSupervisionTimeout;
    String lastFailure;
    for (int round = 0; round < static_cast<int>(sizeof(attempts) / sizeof(attempts[0])); ++round) {
        const GapAutoAttempt& attempt = attempts[round % (sizeof(attempts) / sizeof(attempts[0]))];
        blePreferredOwnAddressType = attempt.ownType;
        blePreferredPeerAddressType = attempt.peerType;
        bleUseDefaultConnParams = attempt.defaultParams;
        bleGapIntervalMin = attempt.intervalMin;
        bleGapIntervalMax = attempt.intervalMax;
        bleGapSupervisionTimeout = attempt.supervisionTimeout;
        ble_gap_disc_params params{};
        params.itvl = 60;
        params.window = 55;
        params.passive = 0;
        params.filter_duplicates = 0;
        params.limited = 0;
        bleGapDiscDone = false;
        int rc = ble_gap_disc(blePreferredOwnAddressType, 1200, &params, bleGapFirstDiscCallback, nullptr);
        if (rc != 0) {
            lastFailure = String("scan round=") + (round + 1) + " " + attempt.label + " start rc=" + rc;
            delay(120);
            continue;
        }
        uint32_t start = millis();
        while (!bleGapDiscDone && bleCandidateCount == 0 && millis() - start < 1400) {
            delay(5);
        }
        if (!bleGapDiscDone) {
            ble_gap_disc_cancel();
            uint32_t cancelStart = millis();
            while (!bleGapDiscDone && millis() - cancelStart < 500) {
                delay(5);
            }
        }
        if (bleCandidateCount > 0 && bleCandidates[0].active) {
            delay(20);
            String gapResult;
            bool ok = bleGapTest(0, gapResult);
            lastFailure = String("scan round=") + (round + 1) + " " + attempt.label + " " + bleScanEntry(0) +
                          "; " + gapResult;
            if (ok) {
                result = lastFailure;
                blePreferredOwnAddressType = savedOwnType;
                blePreferredPeerAddressType = savedPeerType;
                bleUseDefaultConnParams = savedDefaultParams;
                bleGapIntervalMin = savedIntervalMin;
                bleGapIntervalMax = savedIntervalMax;
                bleGapSupervisionTimeout = savedSupervisionTimeout;
                blePairingActive = false;
                bleAutoReconnectPaused = false;
                return true;
            }
            bleCandidateCount = 0;
            for (auto& candidate : bleCandidates) {
                candidate = {};
            }
            delay(120);
        }
    }
    result = lastFailure.length() ? lastFailure : "no pairable BLE HID keyboards found";
    blePreferredOwnAddressType = savedOwnType;
    blePreferredPeerAddressType = savedPeerType;
    bleUseDefaultConnParams = savedDefaultParams;
    bleGapIntervalMin = savedIntervalMin;
    bleGapIntervalMax = savedIntervalMax;
    bleGapSupervisionTimeout = savedSupervisionTimeout;
    blePairingActive = false;
    bleAutoReconnectPaused = false;
    return false;
#else
    result = "BLE HID backend unavailable on ESP32-P4 Arduino build";
    return false;
#endif
}

bool Tab5KeyboardInput::bleGapScanAndSubscribeHid(String& result)
{
#if ENABLE_BLE_HID_KEYBOARD
    _bleAddress = "";
    String scanResult;
    if (!bleGapScanAndTest(scanResult)) {
        result = scanResult;
        return false;
    }
    String hidResult;
    String secureResult;
    bleGapSecure(secureResult);
    if (!bleGapSubscribeHid(hidResult)) {
        result = scanResult + "; " + secureResult + "; " + hidResult;
        return false;
    }
    if (bleCandidateCount > 0 && bleCandidates[0].active) {
        _bleName = bleCandidates[0].name;
        if (!_bleAddress.length()) {
            _bleAddress = bleCandidates[0].address;
            _bleAddressType = bleCandidates[0].addressType;
        }
    }
    bleGapHidPreferred = true;
    _bleRuntimeStatus = String("BLE GAP HID subscribed; reports=") + bleReports;
    _status = _bleRuntimeStatus;
    result = scanResult + "; " + secureResult + "; " + hidResult;
    return true;
#else
    result = "BLE HID backend unavailable on ESP32-P4 Arduino build";
    return false;
#endif
}

bool Tab5KeyboardInput::bleArduinoClientTest(String& result)
{
#if ENABLE_BLE_HID_KEYBOARD
    if (!_bleEnabled) {
        result = "BLE keyboard is disabled; run ble enable first";
        return false;
    }
    String error;
    if (!ensureBleInitialized(error)) {
        result = error;
        return false;
    }
    const bool savedAutoReconnectPaused = bleAutoReconnectPaused;
    bleAutoReconnectPaused = true;
    bleManualScanHoldUntilMs = millis() + 60000;

    BLEUUID hidServiceUuid(static_cast<uint16_t>(BLE_UUID_HID_SERVICE));
    BLEScan* scan = BLEDevice::getScan();
    scan->setActiveScan(true);
    scan->setInterval(1349);
    scan->setWindow(449);
    scan->setDuplicateFilter(false);
    BLEScanResults* results = scan->start(5, false);
    if (!results) {
        result = "arduino scan returned null";
        bleAutoReconnectPaused = savedAutoReconnectPaused;
        return false;
    }

    BLEAdvertisedDevice target;
    bool found = false;
    String seen;
    for (int i = 0; i < results->getCount(); ++i) {
        BLEAdvertisedDevice dev = results->getDevice(i);
        const bool hid = dev.haveServiceUUID() && dev.isAdvertisingService(hidServiceUuid);
        if (seen.length() < 1000) {
            seen += String("\n  ") + i + ": " + dev.getName() + " " + dev.getAddress().toString().c_str() +
                    " type=" + dev.getAddressType() + " conn=" + dev.isConnectable() +
                    (hid ? " hid" : "");
        }
        if (!found && dev.isConnectable() && hid) {
            target = dev;
            found = true;
        }
    }
    if (!found) {
        result = String("arduino no connectable HID; seen=") + seen;
        scan->clearResults();
        bleAutoReconnectPaused = savedAutoReconnectPaused;
        return false;
    }

    BLEClient* client = BLEDevice::createClient();
    if (!client) {
        result = "arduino createClient failed";
        scan->clearResults();
        bleAutoReconnectPaused = savedAutoReconnectPaused;
        return false;
    }
    bool connected = client->connect(&target);
    result = String("arduino target ") + target.getName() + " " + target.getAddress().toString().c_str() +
             " type=" + target.getAddressType() + " connected=" + connected;
    if (connected) {
        BLERemoteService* svc = client->getService(hidServiceUuid);
        result += String(" hidService=") + (svc ? "1" : "0");
        client->disconnect();
    }
    delete client;
    scan->clearResults();
    bleAutoReconnectPaused = savedAutoReconnectPaused;
    return connected;
#else
    result = "BLE HID backend unavailable on ESP32-P4 Arduino build";
    return false;
#endif
}

String Tab5KeyboardInput::bleGapStatus() const
{
#if ENABLE_BLE_HID_KEYBOARD
    if (!bleGapTestConnected || bleGapTestConnHandle == BLE_HS_CONN_HANDLE_NONE) {
        return String("GAP not connected; ") + bleIdentityStatus;
    }
    struct ble_gap_conn_desc desc {};
    int rc = ble_gap_conn_find(bleGapTestConnHandle, &desc);
    if (rc != 0) {
        return String("GAP handle=") + bleGapTestConnHandle + " lookup failed rc=" + rc;
    }
    return String("GAP connected handle=") + bleGapTestConnHandle + " own=" + bleOwnTypeName(blePreferredOwnAddressType) +
           " our_ota=" + desc.our_ota_addr.type + " peer_ota=" + desc.peer_ota_addr.type +
           " peer_id=" + bleAddrToString(desc.peer_id_addr) + " idtype=" + desc.peer_id_addr.type +
           " interval=" + desc.conn_itvl + " latency=" + desc.conn_latency +
           " timeout=" + desc.supervision_timeout + " enc=" + desc.sec_state.encrypted +
           " auth=" + desc.sec_state.authenticated + " bonded=" + desc.sec_state.bonded +
           " key=" + desc.sec_state.key_size;
#else
    return "BLE HID backend unavailable on ESP32-P4 Arduino build";
#endif
}

bool Tab5KeyboardInput::bleGapClose(String& result)
{
#if ENABLE_BLE_HID_KEYBOARD
    if (!bleGapTestConnected || bleGapTestConnHandle == BLE_HS_CONN_HANDLE_NONE) {
        result = "GAP not connected";
        return false;
    }
    int rc = ble_gap_terminate(bleGapTestConnHandle, BLE_ERR_REM_USER_CONN_TERM);
    result = String("gap terminate rc=") + rc + " handle=" + bleGapTestConnHandle;
    bleGapTestConnected = false;
    bleGapTestConnHandle = BLE_HS_CONN_HANDLE_NONE;
    return rc == 0;
#else
    result = "BLE HID backend unavailable on ESP32-P4 Arduino build";
    return false;
#endif
}

bool Tab5KeyboardInput::bleGapSecure(String& result)
{
#if ENABLE_BLE_HID_KEYBOARD
    if (!bleGapTestConnected || bleGapTestConnHandle == BLE_HS_CONN_HANDLE_NONE) {
        result = "GAP not connected";
        return false;
    }
    applyBleSecurityMode();
    int rc = ble_gap_security_initiate(bleGapTestConnHandle);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        result = String("security initiate rc=") + rc + " " + bleGapStatus();
        return false;
    }
    uint32_t start = millis();
    while (millis() - start < 8000) {
        struct ble_gap_conn_desc desc {};
        if (ble_gap_conn_find(bleGapTestConnHandle, &desc) != 0) {
            result = String("security lost connection ") + bleGapTestLog;
            return false;
        }
        const bool wantBond = (blePreferredAuthMode & ESP_LE_AUTH_BOND) != 0;
        if ((!wantBond && desc.sec_state.encrypted) || (wantBond && desc.sec_state.bonded)) {
            _bleAddress = bleAddrToString(desc.peer_id_addr);
            _bleAddressType = desc.peer_id_addr.type;
            if (BleConnection* conn = bleConnectionByHandle(bleGapTestConnHandle)) {
                conn->address = _bleAddress;
            }
            result = String("security ok rc=") + rc + " " + bleGapStatus();
            return true;
        }
        delay(20);
    }
    result = String("security timeout rc=") + rc + " " + bleGapStatus() + bleGapTestLog;
    return false;
#else
    result = "BLE HID backend unavailable on ESP32-P4 Arduino build";
    return false;
#endif
}

bool Tab5KeyboardInput::bleGapListServices(String& result)
{
#if ENABLE_BLE_HID_KEYBOARD
    if (!bleGapTestConnected || bleGapTestConnHandle == BLE_HS_CONN_HANDLE_NONE) {
        result = "GAP not connected";
        return false;
    }
    bleGapHid = {};
    int rc = ble_gattc_disc_all_svcs(bleGapTestConnHandle, bleGapListSvcCallback, &bleGapHid);
    if (rc != 0) {
        result = String("svc list start rc=") + rc;
        return false;
    }
    uint32_t start = millis();
    while (!bleGapHid.done && millis() - start < 5000) {
        delay(10);
    }
    if (!bleGapHid.done) {
        result = String("svc list timeout") + bleGapHid.log;
        return false;
    }
    result = bleGapHid.log.length() ? bleGapHid.log : "no services";
    return bleGapHid.ok;
#else
    result = "BLE HID backend unavailable on ESP32-P4 Arduino build";
    return false;
#endif
}

bool Tab5KeyboardInput::bleGapSubscribeHid(String& result)
{
#if ENABLE_BLE_HID_KEYBOARD
    if (!bleGapTestConnected || bleGapTestConnHandle == BLE_HS_CONN_HANDLE_NONE) {
        result = "GAP not connected";
        return false;
    }
    bleGapHid = {};

    int rc = ble_gattc_disc_all_svcs(bleGapTestConnHandle, bleGapSvcCallback, &bleGapHid);
    if (rc != 0) {
        result = String("svc start rc=") + rc;
        return false;
    }
    uint32_t start = millis();
    while (!bleGapHid.done && millis() - start < 5000) {
        delay(10);
    }
    if (!bleGapHid.serviceStart) {
        result = String("HID service not found") + bleGapHid.log;
        return false;
    }
    if (!bleGapHid.done) {
        bleGapHid.log += " svc.timeout";
        delay(200);
    }

    bleGapHid.done = false;
    bleGapHid.ok = false;
    rc = ble_gattc_disc_all_chrs(bleGapTestConnHandle, bleGapHid.serviceStart, bleGapHid.serviceEnd, bleGapChrCallback, &bleGapHid);
    if (rc != 0) {
        result = String("chr start rc=") + rc + bleGapHid.log;
        return false;
    }
    start = millis();
    while (!bleGapHid.done && millis() - start < 5000) {
        delay(10);
    }
    const uint16_t inputHandle = bleGapHid.bootInputHandle ? bleGapHid.bootInputHandle : bleGapHid.reportInputHandle;
    if (!bleGapHid.ok || inputHandle == 0) {
        result = String("keyboard input characteristic not found") + bleGapHid.log;
        return false;
    }

    if (bleGapHid.protocolModeHandle) {
        uint8_t bootMode = 0;
        int writeRc = ble_gattc_write_no_rsp_flat(bleGapTestConnHandle, bleGapHid.protocolModeHandle, &bootMode, 1);
        bleGapHid.log += String(" proto.write=") + writeRc;
    }

    bleGapHid.done = false;
    bleGapHid.ok = false;
    rc = ble_gattc_disc_all_dscs(bleGapTestConnHandle, inputHandle, bleGapHid.serviceEnd, bleGapDscCallback, &bleGapHid);
    if (rc != 0) {
        result = String("dsc start rc=") + rc + bleGapHid.log;
        return false;
    }
    start = millis();
    while (!bleGapHid.done && millis() - start < 5000) {
        delay(10);
    }
    if (!bleGapHid.ok || bleGapHid.cccdHandle == 0) {
        const uint16_t fallbackCccd = inputHandle + 1;
        if (fallbackCccd > bleGapHid.serviceEnd) {
            result = String("CCCD not found") + bleGapHid.log;
            return false;
        }
        bleGapHid.cccdHandle = fallbackCccd;
        bleGapHid.log += String(" cccd.fallback=") + fallbackCccd;
    }

    uint8_t notifyOn[2] = {0x01, 0x00};
    rc = ble_gattc_write_flat(bleGapTestConnHandle, bleGapHid.cccdHandle, notifyOn, sizeof(notifyOn), nullptr, nullptr);
    if (rc != 0) {
        result = String("subscribe write rc=") + rc + bleGapHid.log;
        return false;
    }
    if (BleConnection* conn = bleConnectionByHandle(bleGapTestConnHandle)) {
        memset(conn->previousKeys, 0, sizeof(conn->previousKeys));
    }
    result = String("HID subscribed input=") + inputHandle + " cccd=" + bleGapHid.cccdHandle + bleGapHid.log;
    _bleRuntimeStatus = String("BLE GAP HID subscribed; reports=") + bleReports;
    _status = _bleRuntimeStatus;
    return true;
#else
    result = "BLE HID backend unavailable on ESP32-P4 Arduino build";
    return false;
#endif
}

bool Tab5KeyboardInput::bleForget(String& result)
{
#if ENABLE_BLE_HID_KEYBOARD
    for (auto& conn : bleConnections) {
        if (conn.active) {
            ble_gap_terminate(conn.handle, BLE_ERR_REM_USER_CONN_TERM);
            conn = {};
        }
    }
    bleGapTestConnected = false;
    bleGapTestConnHandle = BLE_HS_CONN_HANDLE_NONE;
    bleKnownCount = 0;
#endif
    _bleName = "";
    _bleAddress = "";
    _bleAddressType = 1;
    _bleKind = "keyboard";
    bleGapHidPreferred = false;
    _bleRuntimeStatus = _bleEnabled ? "BLE HID enabled; no paired device" : "BLE HID disabled";
    result = "BLE HID pairing list cleared";
    return true;
}

bool Tab5KeyboardInput::bleDisconnect(int index, String& result)
{
#if ENABLE_BLE_HID_KEYBOARD
    if (index < 0) {
        size_t disconnected = 0;
        for (auto& conn : bleConnections) {
            if (conn.active) {
                ble_gap_terminate(conn.handle, BLE_ERR_REM_USER_CONN_TERM);
                conn = {};
                ++disconnected;
            }
        }
        bleGapTestConnected = false;
        bleGapTestConnHandle = BLE_HS_CONN_HANDLE_NONE;
        result = String("BLE disconnected all count=") + disconnected;
        return true;
    }
    if (static_cast<size_t>(index) >= bleKnownCount) {
        result = "invalid device index";
        return false;
    }
    BleConnection* conn = bleConnectionByAddress(bleKnownDevices[index].address);
    if (!conn) {
        result = "device is not connected";
        return false;
    }
    const uint16_t handle = conn->handle;
    int rc = ble_gap_terminate(handle, BLE_ERR_REM_USER_CONN_TERM);
    conn = nullptr;
    if (handle == bleGapTestConnHandle) {
        bleGapTestConnected = false;
        bleGapTestConnHandle = BLE_HS_CONN_HANDLE_NONE;
    }
    result = String("BLE disconnect rc=") + rc + " index=" + index + " handle=" + handle;
    return rc == 0;
#else
    (void)index;
    result = "BLE HID backend unavailable on ESP32-P4 Arduino build";
    return false;
#endif
}

void Tab5KeyboardInput::bleSetConnectTypes(uint8_t ownType, uint8_t peerType)
{
#if ENABLE_BLE_HID_KEYBOARD
    blePreferredOwnAddressType = ownType <= 3 ? ownType : 1;
    blePreferredPeerAddressType = peerType <= 3 ? peerType : 0xFF;
#else
    (void)ownType;
    (void)peerType;
#endif
}

void Tab5KeyboardInput::bleSetSecurity(uint8_t authMode, bool forceSecurity)
{
#if ENABLE_BLE_HID_KEYBOARD
    blePreferredAuthMode = authMode;
    blePreferredForceSecurity = forceSecurity;
    applyBleSecurityMode();
#else
    (void)authMode;
    (void)forceSecurity;
#endif
}

void Tab5KeyboardInput::bleSetGapParams(uint16_t scanInterval, uint16_t scanWindow, uint16_t intervalMin,
                                        uint16_t intervalMax, uint16_t latency, uint16_t supervisionTimeout)
{
#if ENABLE_BLE_HID_KEYBOARD
    bleGapScanInterval = max<uint16_t>(4, scanInterval);
    bleGapScanWindow = min<uint16_t>(max<uint16_t>(4, scanWindow), bleGapScanInterval);
    bleGapIntervalMin = max<uint16_t>(6, intervalMin);
    bleGapIntervalMax = max<uint16_t>(bleGapIntervalMin, intervalMax);
    bleGapLatency = latency;
    bleGapSupervisionTimeout = max<uint16_t>(10, supervisionTimeout);
#else
    (void)scanInterval;
    (void)scanWindow;
    (void)intervalMin;
    (void)intervalMax;
    (void)latency;
    (void)supervisionTimeout;
#endif
}

void Tab5KeyboardInput::push(const KeyAction& action)
{
    if (action.type == KeyActionType::None) {
        return;
    }
    const size_t next = (_head + 1) % QueueSize;
    if (next == _tail) {
        _tail = (_tail + 1) % QueueSize;
    }
    _queue[_head] = action;
    _head = next;
}
