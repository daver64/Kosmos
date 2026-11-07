# Preparing a GitHub beta release

These steps prepare files locally. They do not create a release, push a tag,
publish the website, or advance `VERSION`.

1. Review the changes since the last published build and write
   `docs/release-notes/<VERSION>.md`.
2. Build the two application archives with `scripts/release.sh -j 1` and
   build both SDK archives with `scripts/build-sdk.sh`. Create a fresh
   high-resolution pack with `scripts/pack-hires.sh OUTPUT.tar.gz`.
3. Audit the actual contents of each application archive, both SDK archives,
   and the pack: identify each third-party asset's source, licence, required
   attribution and permission to redistribute. The packer excludes the Main
   Belt Asteroids add-on's Ida, Gaspra, Kleopatra and Toutatis assets, but
   that exclusion is **not** a clearance of the remaining assets. The older
   website pack contains those excluded assets and must not be used as-is.
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

   The staging tool checks embedded archive versions and the audit hashes,
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
