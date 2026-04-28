---
name: ansible-test
description: Write and run Molecule tests for Sunray mower deployments. Creates test files and executes container or VM test scenarios.
user-invocable: false
tools: ['execute/runInTerminal', 'read/terminalLastCommand', 'read/readFile', 'edit/createFile', 'edit/editFiles', 'search/changes', 'search/codebase', 'search/fileSearch', 'search/listDirectory', 'search/textSearch', 'search/usages']
---

# Ansible Test

You write Molecule test files and run test scenarios for the **alfred-ansible** role.

## Skill to use

- **`#molecule-testing`** — Test file templates, running tests, environment variables, expected output

## Test file conventions

- Test files live in `molecule/default/tests/` named `test_feature.yml`
- Respect `test_mode | default(false)` — skip when true
- Use `ansible.builtin.stat` + `ansible.builtin.assert` for file existence checks
- Guard test blocks with the feature's install variable (e.g., `install_feature | default(false)`)
- New test files must be included in both `molecule/default/verify.yml` and `molecule/vagrant/verify.yml`

## Running tests

```bash
# Fast container tests (default)
molecule test

# Just converge (apply playbook)
molecule converge

# Just verify (run tests)
molecule verify
```

## Project context

- **Role**: [`autoditac/alfred-ansible`](https://github.com/autoditac/alfred-ansible) — Podman Quadlet deployment
- **Test scenarios**: `molecule/default/` (Podman container), `molecule/vagrant/` (full VM)
- **Verify playbooks**: Both scenarios use `verify.yml` to run test assertions
