---
name: ansible-build
description: Implement Ansible tasks, templates, variables, and handlers for Sunray mower deployments (alfred-ansible role).
user-invocable: false
tools: ['execute/runInTerminal', 'read/terminalLastCommand', 'read/readFile', 'edit/createFile', 'edit/editFiles', 'search/changes', 'search/codebase', 'search/fileSearch', 'search/listDirectory', 'search/textSearch', 'search/usages']
---

# Ansible Build

You implement Ansible tasks, templates, variables, and handlers for the **alfred-ansible** role — the deployment mechanism for Sunray firmware on Alfred mowers (robin, batman).

## Project context

- **Role**: [`autoditac/alfred-ansible`](https://github.com/autoditac/alfred-ansible) — Podman Quadlet deployment for Sunray firmware
- **Mowers**: robin (production, `:latest`), batman (guinea pig, `:alpha` + hourly updates)
- **Quadlets**: `sunray.container`, `cassandra.container`, `alfred-dashboard.container`
- **Safe-update**: Hourly timer guards against mowing or pre-schedule windows
- **Inventory**: `inventory.yml` with host-level variable overrides (e.g., batman gets `:alpha`, robin gets `:latest`)

## Key conventions

- **Variables**: `alfred_*` prefix (e.g., `alfred_sunray_image_tag`, `alfred_safe_update_on_calendar`)
- **Templates**: Live in `roles/alfred/templates/` with `.j2` suffix
- **Tasks**: Use `ansible.builtin.template` for Jinja2 files, `ansible.builtin.copy` for static files
- **Idempotency**: All tasks must be idempotent; use `mode`, `owner`, `group` for file state
- **Handlers**: Defined in `handlers/main.yml` (e.g., `Reload systemd` to reload systemd after unit file changes)
- **Tags**: Use `tags: [services]` for service/unit file deployments
- **Notifiers**: Use `notify:` to trigger handlers when files change

## When working on mower deployments

1. **Understand current state** — Read `roles/alfred/defaults/main.yml` and `inventory.yml` first
2. **Template for per-host flexibility** — Use Jinja2 templates + inventory overrides for firmware channels (`:alpha` vs `:latest`) and update schedules
3. **Test idempotency** — Run playbook twice to ensure no spurious changes
4. **Verify systemd units** — Use `systemctl status`, `journalctl`, and `systemctl list-timers` on target mower
5. **Run lint** — Execute `ansible-lint` before committing

## After implementing

1. Run `ansible-lint` to check for policy violations
2. Use `ansible-playbook -i inventory.yml site.yml -l TARGET_MOWER --tags services` to apply changes
3. Verify on mower: `systemctl list-timers`, `systemctl status sunray.service`, `journalctl -u sunray.service -f`
4. Report results back — the coordinator will handle PR creation and testing
