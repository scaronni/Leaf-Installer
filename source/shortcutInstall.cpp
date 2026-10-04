#include <switch.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "ui/MainApplication.hpp"
#include "nx/ipc/tin_ipc.h"
#include "util/crypto.hpp"
#include "util/util.hpp"
#include "util/lang.hpp"
#include "shortcutInstall.hpp"

namespace inst::ui {
    extern MainApplication *mainApp;
}

// libnx defaults to __nx_applet_exit_mode = 0 for NROs: on exit it just returns
// to the loader without the SelfController::Exit handshake. Fine under hbmenu,
// but the forwarder owns an application process, so AM sees it vanish and
// reports a crash. Never applied to hbmenu / title takeover launches.
extern "C" u32 __nx_applet_exit_mode;

// HOME Menu shortcut: installs a tiny forwarder application (a patched
// nx-hbloader that boots our NRO) straight through NCM. The forwarder ships as
// a key-free plaintext template built by shortcut/build-shortcut.py; here we
// seal its NCA headers with the console's own header key, fix up every hash
// that depends on them (content records, CNMT digest, PFS0 hash table, FS
// header hash) and register the three NCAs. Approach adapted from NSteamLink
// (https://github.com/kxn/nsteamlink, GPLv3). Launching it needs sig patches.
namespace shortcut {
    static constexpr u64 kShortcutId = 0x01004C4541460000;
    static const char kTemplatePath[] = "romfs:/shortcut/template.bin";
    static const char kNroPath[] = "sdmc:/switch/Leaf-Installer/Leaf-Installer.nro";
    static const char kMagic[8] = {'L', 'E', 'A', 'F', 'S', 'C', '0', '1'};

    enum class Outcome { Added, Exists, Failed, CleanupFailed };

    // Program, control, meta — the order the template stores them in.
    struct Package {
        std::vector<u8> data[3];
        u8 hashes[3][SHA256_HASH_SIZE];
        u8 database[96]; // NcmContentMetaHeader + application ext header + 3 NcmContentInfo
    };

    static u64 le(const u8 *p, int n) {
        u64 v = 0;
        for (int i = n - 1; i >= 0; --i) v = (v << 8) | p[i];
        return v;
    }

    static void put(u8 *p, u64 v, int n) {
        for (int i = 0; i < n; ++i, v >>= 8) p[i] = (u8)v;
    }

    static bool inRange(u64 offset, u64 length, u64 total) {
        return offset <= total && length <= total - offset;
    }

    // The first 0xC00 bytes of an NCA (header + 4 FS headers) are AES-XTS
    // encrypted with the header key, 0x200-byte sectors, Nintendo tweak.
    static void seal(const u8 *headerKey, u8 *nca) {
        Crypto::AesXtr(headerKey, true).encrypt(nca, nca, 0xC00, 0, 0x200);
    }

    static bool materialize(Package &p, const std::vector<u8> &blob, const u8 *headerKey) {
        if (blob.size() < 20 || blob.size() > 16 * 1024 * 1024 || memcmp(blob.data(), kMagic, 8)) return false;
        // NCA header content type (0x205) — NOT libnx's NcmContentType numbering.
        const u8 ncaTypes[3] = {0 /* program */, 2 /* control */, 1 /* meta */};
        size_t pos = 20;
        for (int i = 0; i < 3; ++i) {
            const u64 n = le(blob.data() + 8 + 4 * i, 4);
            if (n < 0xC00 || !inRange(pos, n, blob.size())) return false;
            const u8 *h = blob.data() + pos;
            if (memcmp(h + 0x200, "NCA3", 4) || h[0x205] != ncaTypes[i] || le(h + 0x208, 8) != n ||
                le(h + 0x210, 8) != kShortcutId || h[0x404] != 1)
                return false;
            p.data[i].assign(h, h + n);
            pos += n;
        }
        if (pos != blob.size()) return false;

        // Meta NCA section 0: HierarchicalSha256 PFS0 holding one 192-byte CNMT.
        u8 *meta = p.data[2].data(), *fs = meta + 0x400, *sb = fs + 8;
        const u64 metaSize = p.data[2].size();
        const u64 section = le(meta + 0x240, 4) * 0x200, end = le(meta + 0x244, 4) * 0x200;
        const u64 table = le(sb + 0x28, 8), tableSize = le(sb + 0x30, 8);
        const u64 offset = le(sb + 0x38, 8), length = le(sb + 0x40, 8);
        const u64 block = le(sb + 0x20, 4);
        if (section < 0xC00 || end > metaSize || end < section || fs[2] != 1 || fs[3] != 2 ||
            !inRange(table, tableSize, end - section) || !inRange(offset, length, end - section) ||
            !block || block > 0x100000 || length < 40 || table + tableSize > offset ||
            tableSize != ((length + block - 1) / block) * SHA256_HASH_SIZE)
            return false;
        u8 *pfs = meta + section + offset;
        if (memcmp(pfs, "PFS0", 4) || le(pfs + 4, 4) != 1) return false;
        const u64 dataStart = 40 + le(pfs + 8, 4);
        const u64 fileOff = le(pfs + 16, 8), fileSize = le(pfs + 24, 8);
        if (!inRange(dataStart, fileOff, length) || !inRange(dataStart + fileOff, fileSize, length) || fileSize != 192)
            return false;
        u8 *cnmt = pfs + dataStart + fileOff;
        if (le(cnmt, 8) != kShortcutId || le(cnmt + 8, 4) != 0 || cnmt[12] != NcmContentMetaType_Application ||
            le(cnmt + 14, 2) != 16 || le(cnmt + 16, 2) != 2 || le(cnmt + 18, 2) != 0)
            return false;

        // Seal program + control, then point the CNMT content records at them
        // (content ID = first half of the SHA-256 of the sealed NCA).
        const u8 contentTypes[3] = {NcmContentType_Program, NcmContentType_Control, NcmContentType_Meta};
        for (int i = 0; i < 2; ++i) {
            seal(headerKey, p.data[i].data());
            sha256CalculateHash(p.hashes[i], p.data[i].data(), p.data[i].size());
            u8 *record = cnmt + 48 + 56 * i;
            memcpy(record, p.hashes[i], 32);
            memcpy(record + 32, p.hashes[i], 16);
            put(record + 48, p.data[i].size(), 6);
            record[54] = contentTypes[i];
            record[55] = 0;
        }
        // Re-hash bottom-up: CNMT digest → PFS0 block hashes → master hash → FS header hash.
        sha256CalculateHash(cnmt + 160, cnmt, 160);
        for (u64 off = 0, i = 0; off < length; off += block, ++i)
            sha256CalculateHash(meta + section + table + i * SHA256_HASH_SIZE, pfs + off, length - off < block ? length - off : block);
        sha256CalculateHash(sb, meta + section + table, tableSize);
        sha256CalculateHash(meta + 0x280, fs, 0x200);
        seal(headerKey, meta);
        sha256CalculateHash(p.hashes[2], meta, metaSize);

        // Content meta database entry: meta first, then program, control.
        memset(p.database, 0, sizeof(p.database));
        put(p.database, 16, 2);
        put(p.database + 2, 3, 2);
        memcpy(p.database + 8, cnmt + 32, 16);
        for (int i = 0; i < 3; ++i) {
            const int index = (i + 2) % 3;
            u8 *info = p.database + 24 + i * 24;
            memcpy(info, p.hashes[index], 16);
            put(info + 16, p.data[index].size(), 6);
            info[22] = contentTypes[index];
        }
        return true;
    }

    static bool nroPresent() {
        FILE *f = fopen(kNroPath, "rb");
        if (!f) return false;
        u8 h[0x14];
        const bool ok = fread(h, 1, sizeof(h), f) == sizeof(h) && !memcmp(h + 0x10, "NRO0", 4);
        fclose(f);
        return ok;
    }

    static Result metaDatabaseEmpty(NcmContentMetaDatabase *db, bool *empty) {
        NcmContentMetaKey key;
        s32 total = 0, count = 0;
        const Result rc = ncmContentMetaDatabaseList(db, &total, &count, &key, 1, NcmContentMetaType_Unknown,
                                                     kShortcutId, kShortcutId, kShortcutId, NcmContentInstallType_Full);
        *empty = total == 0;
        return rc;
    }

    // Expects inst::util::initInstallServices() (ncm, nsext, splCrypto).
    static Outcome install(std::string &detail) {
        Result rc = 0;
        const char *stage = "ns";
        Outcome outcome = Outcome::Failed;
        bool metadata = false, cleanupFailed = false, installed[3] = {false, false, false};
        NcmContentStorage storage = {};
        NcmContentMetaDatabase db = {}, other = {};
        NcmContentId ids[3] = {};
        NcmContentMetaKey key = {};
        key.id = kShortcutId;
        key.type = NcmContentMetaType_Application;
        key.install_type = NcmContentInstallType_Full;
        Package package;
        bool exists = false, empty = false;

#define TRY(call) do { rc = (call); if (R_FAILED(rc)) goto done; } while (0)
        TRY(nsIsAnyApplicationEntityInstalled(kShortcutId, &exists));
        if (exists) { outcome = Outcome::Exists; goto done; }
        // Archived records too, not just installed content.
        for (s32 offset = 0;;) {
            NsApplicationRecord records[32];
            s32 count = 0;
            TRY(nsListApplicationRecord(records, 32, offset, &count));
            for (s32 i = 0; i < count; ++i)
                if (records[i].application_id == kShortcutId) { outcome = Outcome::Exists; goto done; }
            if (count < 32) break;
            offset += count;
        }
        stage = "ncm";
        TRY(ncmOpenContentMetaDatabase(&db, NcmStorageId_SdCard));
        TRY(metaDatabaseEmpty(&db, &empty));
        if (!empty) { outcome = Outcome::Exists; goto done; }
        TRY(ncmOpenContentMetaDatabase(&other, NcmStorageId_BuiltInUser));
        TRY(metaDatabaseEmpty(&other, &empty));
        if (!empty) { outcome = Outcome::Exists; goto done; }
        TRY(ncmOpenContentStorage(&storage, NcmStorageId_SdCard));

        stage = "template";
        {
            std::ifstream in(kTemplatePath, std::ios::binary);
            const std::vector<u8> blob((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (!materialize(package, blob, Crypto::Keys().headerKey)) goto done;
        }
        for (int i = 0; i < 3; ++i) {
            memcpy(&ids[i], package.hashes[i], sizeof(NcmContentId));
            TRY(ncmContentStorageHas(&storage, &exists, &ids[i]));
            if (exists) { outcome = Outcome::Exists; goto done; }
        }

        stage = "content";
        for (int i = 0; i < 3; ++i) {
            NcmPlaceHolderId placeholder;
            TRY(ncmContentStorageGeneratePlaceHolderId(&storage, &placeholder));
            TRY(ncmContentStorageCreatePlaceHolder(&storage, &ids[i], &placeholder, package.data[i].size()));
            for (size_t off = 0; off < package.data[i].size() && R_SUCCEEDED(rc); off += 0x10000) {
                const size_t len = std::min<size_t>(package.data[i].size() - off, 0x10000);
                rc = ncmContentStorageWritePlaceHolder(&storage, &placeholder, off, package.data[i].data() + off, len);
            }
            if (R_SUCCEEDED(rc)) rc = ncmContentStorageRegister(&storage, &ids[i], &placeholder);
            if (R_SUCCEEDED(rc)) installed[i] = true;
            else {
                if (R_FAILED(ncmContentStorageDeletePlaceHolder(&storage, &placeholder))) cleanupFailed = true;
                goto done;
            }
        }

        stage = "metadata";
        TRY(ncmContentMetaDatabaseSet(&db, &key, package.database, sizeof(package.database)));
        metadata = true;
        TRY(ncmContentMetaDatabaseCommit(&db));

        stage = "record";
        {
            ContentStorageRecord record = {};
            record.metaRecord = key;
            record.storageId = NcmStorageId_SdCard;
            TRY(nsPushApplicationRecord(kShortcutId, NsApplicationRecordType_Installed, &record, 1));
        }
        outcome = Outcome::Added;
#undef TRY

    done:
        if (outcome != Outcome::Added) {
            bool rollbackOk = true;
            if (metadata) {
                rollbackOk = R_SUCCEEDED(ncmContentMetaDatabaseRemove(&db, &key));
                if (R_FAILED(ncmContentMetaDatabaseCommit(&db))) rollbackOk = false;
            }
            // If the metadata couldn't be removed, keep its content so it doesn't dangle.
            if (rollbackOk)
                for (int i = 0; i < 3; ++i)
                    if (installed[i] && R_FAILED(ncmContentStorageDelete(&storage, &ids[i]))) rollbackOk = false;
            if (!rollbackOk || cleanupFailed) outcome = Outcome::CleanupFailed;
            if (outcome == Outcome::Failed || outcome == Outcome::CleanupFailed) {
                char buf[64];
                snprintf(buf, sizeof(buf), "%s (0x%08X)", stage, rc);
                detail = buf;
            }
        }
        ncmContentMetaDatabaseClose(&other);
        ncmContentMetaDatabaseClose(&db);
        ncmContentStorageClose(&storage);
        return outcome;
    }

    void configureHomeExit() {
        u64 programId = 0;
        if (!envIsNso() && R_SUCCEEDED(svcGetInfo(&programId, InfoType_ProgramId, CUR_PROCESS_HANDLE, 0)) &&
            programId == kShortcutId)
            __nx_applet_exit_mode = 1;
    }

    void startShortcutInstall() {
        auto *app = inst::ui::mainApp;
        if (!nroPresent()) {
            app->CreateShowDialog("shortcut.nro_missing_title"_lang, "shortcut.nro_missing_desc"_lang, {"common.ok"_lang}, true);
            return;
        }
        if (app->CreateShowDialog("shortcut.confirm_title"_lang, "shortcut.confirm_desc"_lang, {"shortcut.add"_lang, "common.cancel"_lang}, false) != 0)
            return;

        std::string detail;
        inst::util::initInstallServices();
        const Outcome outcome = install(detail);
        inst::util::deinitInstallServices();

        switch (outcome) {
            case Outcome::Added:
                app->CreateShowDialog("shortcut.success_title"_lang, "shortcut.success_desc"_lang, {"common.ok"_lang}, true);
                break;
            case Outcome::Exists:
                app->CreateShowDialog("shortcut.exists_title"_lang, "shortcut.exists_desc"_lang, {"common.ok"_lang}, true);
                break;
            case Outcome::Failed:
                app->CreateShowDialog("shortcut.failed_title"_lang, "shortcut.failed_desc"_lang + detail, {"common.ok"_lang}, true);
                break;
            case Outcome::CleanupFailed:
                app->CreateShowDialog("shortcut.failed_title"_lang, "shortcut.cleanup_failed_desc"_lang + detail, {"common.ok"_lang}, true);
                break;
        }
    }
}
