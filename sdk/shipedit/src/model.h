// ShipEdit's document model: a ship.json held as an order-preserving JSON tree, so every key the editor does not know about
// (cockpit, systems, animations, surfaces, scripts) is written back untouched. No GTK in here, so it is unit-tested headless.
#pragma once

#include <json.hpp>

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace shipedit
{
    using json = nlohmann::ordered_json;
    namespace fs = std::filesystem;

    struct AddonInfo
    {
        std::string id, name;
        fs::path dir;
        std::vector<std::string> ships; // file names listed in addon.json
    };

    /** Every folder under `addons_dir` with an addon.json that lists at least one ship, sorted by folder name. */
    std::vector<AddonInfo> scan_addons(const fs::path &addons_dir);

    struct StageStats
    {
        double dry = 0, propellant = 0, thrust = 0;
        double mass_at_ignition = 0; // this stage and everything above it
        double isp = 0, delta_v = 0, twr = 0;
        bool air_breathing = false;
    };

    struct Mesh
    {
        std::vector<float> xyz;          // 3 per vertex
        std::vector<unsigned> triangles; // 3 indices per triangle
        bool empty() const { return triangles.empty(); }
    };

    /** Reads a Wavefront OBJ or a binary glTF (.glb) into one triangle soup in the file's own axes. Returns false with `error` set on failure. */
    bool load_mesh(const fs::path &file, Mesh &out, std::string &error);

    class Document
    {
    public:
        /** Starts from an addon's ship file. `ship_file` is a name from addon.json. */
        bool load(const AddonInfo &addon, const std::string &ship_file, std::string &error);
        /** A blank one-stage ship with no source addon. */
        void make_blank(const std::string &name);

        bool has_source() const { return !source_dir_.empty(); }
        const fs::path &source_dir() const { return source_dir_; }
        const std::string &source_ship_file() const { return source_ship_file_; }
        const std::string &source_addon_id() const { return source_addon_id_; }
        const std::string &source_ship_name() const { return source_ship_name_; }

        json &root() { return root_; }
        const json &root() const { return root_; }
        json &stages();
        int stage_count() const;
        std::string name() const { return root_.value("name", std::string()); }

        // Undo and redo take snapshots of the whole tree; call checkpoint() before each edit.
        void checkpoint();
        bool undo();
        bool redo();
        bool can_undo() const { return !undo_.empty(); }
        bool can_redo() const { return !redo_.empty(); }
        bool dirty() const { return dirty_; }
        void mark_clean() { dirty_ = false; }

        // Stage list edits.
        int add_stage(); // returns the new index (at the end)
        void remove_stage(int index);
        int duplicate_stage(int index); // the copy goes right after the original
        bool move_stage(int index, int delta);
        /** Reorder and repack a simple rocket, without modifying ship-wide equipment. */
        bool arrange_stage(int from, int to, std::string &error);
        /** Import a stage at the given staging position; -1 appends at the nose end. */
        bool import_stage(const Document &source, int index, int position, int &inserted, std::string &error, std::string &warning, bool overlap_previous = false);
        /** Expand part-kit stages for the stage palette without changing the editable source document. */
        bool expanded_root(json &out, std::string &error) const;
        /** Moves a stage along Z and, with `with_contents`, its parts, engines and gear too. */
        void set_stage_z(int index, double z, bool with_contents);
        /** Stage 0 stays where it is; each later stage is stacked against the one before it, contents moving with it. */
        void auto_pack();

        // Parts
        /** Adds a mesh part for a model file. A file outside the source addon is queued to be copied into models/ on save. */
        void add_model_part(int stage, const fs::path &file);
        void add_prop_part(int stage, const std::string &prop);
        void add_engine(int stage);
        void add_gear(int stage);

        /** Where a path written in the ship file really is: the source addon, a file queued for copying, or empty if missing. */
        fs::path resolve(const std::string &relative) const;
        const std::map<std::string, fs::path> &pending_copies() const { return pending_; }

        std::vector<StageStats> stats() const;
        /** Human-readable problems: missing models, a stage with no mass, engines outside their stage, a name clash. */
        std::vector<std::string> validate() const;

        /** Writes a new addon folder `dest_root/id`: a copy of the source addon (when there is one) with this ship written over the one that was edited,
         *  a renamed addon.json (other ships in the source stay listed), and any queued models. Refuses to touch an existing folder unless `overwrite`. */
        bool save_as(const fs::path &dest_root, const std::string &id, const std::string &addon_name, bool overwrite, fs::path &written_dir, std::string &error);

        /** Writes the edited ship back into the addon it was loaded from, after copying the whole addon folder to `backup_root/<id>-<timestamp>`
         *  (which must lie outside the addon). addon.json and every other file are left as they were; queued models are copied in. */
        bool save_in_place(const fs::path &backup_root, fs::path &backup_dir, std::string &error, bool force = false);
        /** True when the ship file on disk has changed since it was loaded (something else edited it). */
        bool changed_on_disk() const;

    private:
        json root_;
        fs::path source_dir_;
        std::string source_ship_file_, source_addon_id_, source_ship_name_;
        std::map<std::string, fs::path> pending_;
        fs::file_time_type loaded_time_{};
        struct Snapshot
        {
            json root;
            std::map<std::string, fs::path> pending;
        };
        std::vector<Snapshot> undo_, redo_;
        bool dirty_ = false;
    };

    /** An id fit for a folder name: lower case letters, digits and dashes. */
    std::string slug(const std::string &text);
} // namespace shipedit
