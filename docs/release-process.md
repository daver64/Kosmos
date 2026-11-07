# Preparing a GitHub beta release

These steps prepare files locally. They do not create a release, push a tag,
publish the website, or advance `VERSION`.

1. Review the changes since the last published build and write
   `docs/release-notes/<VERSION>.md`.
2. Build the two application archives with `scripts/release.sh -j 1` and
   build both SDK archives with `scripts/build-sdk.sh`. To preserve existing
   SDK archives, use `JOBS=1 OUT=/path/to/new-sdk-output sh scripts/build-sdk.sh`.
   Create a procedural-only texture pack with
   `python3 scripts/pack-procedural-hires.py OUTPUT.tar.gz`. It generates
   fresh, fictional maps rather than using the mixed local hi-res directory;
   Earth and other bodies will not have their real surface geography in these
   optional overrides. These maps do not include surface tiles or cloud packs.
   They install separately into `data/textures-generated/`; existing
   `data/textures-hires/` overrides and local Celestia source maps take priority.
3. Audit the actual contents of each application archive, both SDK archives,
   and the pack: identify each third-party asset's source, licence, required
   attribution and permission to redistribute. See the incomplete
   [asset provenance inventory](asset-provenance.md) for documented findings
   and unresolved families. Application packaging copies the working tree,
   including ignored assets unless explicitly excluded, so inspect the
   archives themselves rather than assuming they match the Git index.
   `python3 scripts/audit-package-files.py /path/to/application-archive`
   lists packaged files under the source asset directories that are absent
   from the current Git index. Review every result, including expected
   generated binaries; this name-only comparison neither checks contents
   nor establishes licences, and tracked files still require review.
   The packer excludes the Main
   Belt Asteroids add-on's Ida, Gaspra, Kleopatra and Toutatis assets, but
   that exclusion is **not** a clearance of the remaining assets. The older
   website pack and packs built by `scripts/pack-hires.sh` may contain
   third-party data and are rejected by release staging.
   The local source inventory is incomplete: for example,
   `import/open-game-art/textures/metal/readme.txt` identifies the Majestical
   textures as CC BY-SA 4.0, while individual Celestia packs have their own
   terms. `import/orbiter/CVELAtlas/Models/AtlasIII/The Artistic License.txt`
   is another item to investigate, not proof that every CVEL asset shares
   those terms. Match evidence and required notices to the actual packaged
   files; neither a free release nor a project-level licence clears all media.
   In particular, the Ariane 5 source notice in
   `import/orbiter/CVELAriane5/add-on docs/CVE-Lite/Ariane5v2.txt` says that
   Thomas Ruth's Ariane meshes are for non-commercial applications only,
   while other meshes have different terms. The LGPL notice for the code
   does not establish a licence for those meshes. Do not treat the Ariane
   add-on or footage showing it as cleared until its actual assets have
   been traced and removed or their terms reviewed. The current application
   package build excludes the whole Ariane add-on, including ATV, and its
   two scenarios; staging rejects archives containing them. They have also
   been removed from this working tree; source imports and old packages can
   still contain them.
   The GitHub `main` and `old-history` branches were rewritten to remove
   identified expunged media and superseded texture versions. Other assets
   still need review, and local refs, old archives and server caches may
   retain earlier objects.
   The separate `data/textures/planets/` directory contains old downloaded
   images with conflicting licence descriptions in its documentation. The
   simulator's surface loader does not use this directory, but KosmosMap can
   use local copies. It is excluded from
   new application packages and rejected by staging, but local copies and
   Git history can still contain it. This does not clear the active fallback textures
   elsewhere in `data/textures/`.
   The local `data/reference/` collection and `data/textures/skybox/` are
   also excluded from new application packages and rejected by staging.
   They were removed from the Git index; the skybox was also removed from
   this working tree. Local reference files and history still contain media.
   The four Deltaglider `Shuttle_BRI`, `Shuttle_CAN`,
   `Shuttle_FRA` and `Shuttle_USA` skin folders are likewise excluded;
   their source archive has no licence notice, and the bundled Chippenham
   scenario no longer selects one. They were also removed from this working
   tree; the original import archive and Git history remain.
   The sixteen unverified root-level planetary `data/textures/*_normal.jpg`
   maps are removed from the Git index and local working tree and excluded
   from packages and staging. Installations without replacements have no
   normal-map relief for those bodies.
   The Orbiter-Mods download page offers Orbiter high-resolution terrain but
   does not itself specify licence terms for the packs. A launcher link to
   that independent source is not evidence that Kosmos may redistribute
   them. The original Jestr Earth JPG archive identifies NASA Blue Marble
   as the source of its modified levels 0-5 but does not state a licence
   for that derivative. The separate levels 6-10 readme names several
   imagery sources; public availability does not by itself establish
   redistribution permission for the combined tiles.
4. Record the review in a JSON file with an entry for each exact input
   filename. Each entry must include the input's SHA-256, `approved: true`,
   and a nonempty `evidence` reference to the actual review. For example:

   ```json
   {
     "assets": {
       "kosmos-hires.tar.gz": {
         "sha256": "<64 lowercase hexadecimal characters>",
         "approved": true,
         "evidence": "path or reference to the asset-by-asset rights review"
       }
     }
   }
   ```

   Add equivalent entries for the Linux and Windows application archives and
   both SDK archives. A declaration is not a substitute for the underlying
   rights evidence. Calculate hashes after the final builds; rebuilding any
   asset requires reviewing and updating its hash.
5. Stage the already-built and audited files into a **new** directory:

   ```sh
   sh scripts/release.sh --stage-only /path/to/new-stage \
     --pack /path/to/kosmos-hires.tar.gz --rights-audit /path/to/audit.json
   ```

   SDK archives built outside `build/package` can be selected with
   `--sdk-linux /path/to/kosmos-sdk.tar.gz --sdk-windows /path/to/kosmos-sdk.zip`.

   The staging tool permits only the nine procedural-map filenames and their
   separate credit file in the texture pack; filenames alone do not prove
   provenance, so retain evidence that the generator produced the exact
   archive being audited. The staging tool checks embedded archive versions and the audit hashes,
   splits the pack into ordered parts of at most 1800 MiB and writes
   `manifest.json` and `SHA256SUMS.txt`. It refuses to overwrite a staging
   directory; an unsuccessful attempt is incomplete and must not be uploaded.
   The reconstructed archive's size and hash are recorded in the manifest.
   GitHub requires each release asset to be strictly under 2 GiB; see
   [GitHub's release documentation](https://docs.github.com/en/repositories/releasing-projects-on-github/about-releases).
6. After independently verifying the staged files, rights and release notes,
   the owner can create a **draft** GitHub release tagged `v<VERSION>`, upload
   every staged file unchanged, check the draft and publish it as a beta
   prerelease. The launcher includes prereleases in discovery and uses the
   manifest and downloads from the selected exact tag.

Only after the owner has confirmed the release is actually published should
they manually advance `VERSION` to the next build number. Staging, failed
uploads and unpublished drafts must not change it. The old website remains
untouched until the owner retires it separately; multiplayer and KosmosAV are
independent services and should be assessed independently.
