# RTCon_general

RTCon for a general library.

## Getting started

1. Drop your project source under `./project/<your_project>/`.
2. Add a recipe for it to `config.yml` under `projects:` and set `active_project` to its key.
3. Build and run:

   ```
   docker compose build
   docker compose run --rm rtcon
   ```

To switch projects, change `active_project` in `config.yml` — `docker-compose.yml` does not need to be edited.
