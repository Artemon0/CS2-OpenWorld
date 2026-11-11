#!/usr/bin/env bash
# CS2 Open World dependency installer (Linux / macOS).
#
# The script never installs anything silently: it detects what is already available, asks
# before every change, and asks for a path when a tool exists but is not on PATH.
#
#   scripts/install.sh              # interactive installation
#   scripts/install.sh --check      # report only; make no changes
#   scripts/install.sh --yes        # skip confirmations (for CI/automation)
#
# Windows: scripts/install.ps1

set -uo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd -- "${SCRIPT_DIR}/.." && pwd)

ASSUME_YES=0
CHECK_ONLY=0
ALLOW_INSTALL=1
SKIP_REDIS=0
SKIP_POSTGRES=0
SKIP_BUILD=0
BUILD_DIR="build-linux"

MISSING=()
NOTES=()

for argument in "$@"; do
	case "$argument" in
		--check) CHECK_ONLY=1; ALLOW_INSTALL=0 ;;
		--yes|-y) ASSUME_YES=1 ;;
		--no-install) ALLOW_INSTALL=0 ;;
		--skip-redis) SKIP_REDIS=1 ;;
		--skip-postgres) SKIP_POSTGRES=1 ;;
		--skip-build) SKIP_BUILD=1 ;;
		--build-dir) shift ;;
		--build-dir=*) BUILD_DIR="${argument#*=}" ;;
		--help|-h)
			sed -n '2,12p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
			exit 0
			;;
		*)
			echo "Unknown argument: $argument (see --help)" >&2
			exit 2
			;;
	esac
done

if [ -t 1 ]; then
	BOLD=$'\033[1m'; RED=$'\033[31m'; GREEN=$'\033[32m'; YELLOW=$'\033[33m'; RESET=$'\033[0m'
else
	BOLD=""; RED=""; GREEN=""; YELLOW=""; RESET=""
fi

say() { printf '%s\n' "$*"; }
step() { printf '\n%s==> %s%s\n' "$BOLD" "$*" "$RESET"; }
ok() { printf '  %s[ok]%s %s\n' "$GREEN" "$RESET" "$*"; }
warn() { printf '  %s[!]%s %s\n' "$YELLOW" "$RESET" "$*"; NOTES+=("$*"); }
bad() { printf '  %s[missing]%s %s\n' "$RED" "$RESET" "$*"; MISSING+=("$*"); }

ask() {
	# ask "question" [default-y|default-n] -> returns 0 for yes
	local question=$1 default=${2:-default-n} answer
	if [ "$ASSUME_YES" -eq 1 ] || [ ! -t 0 ]; then
		[ "$default" = "default-y" ] && return 0
		return 1
	fi
	local suffix="[y/N]"
	[ "$default" = "default-y" ] && suffix="[Y/n]"
	while true; do
		printf '  %s %s ' "$question" "$suffix" >&2
		read -r answer
		case "$answer" in
			y|Y|yes|YES|д|Д|да|ДА) return 0 ;;
			n|N|no|NO|н|Н|нет|НЕТ) return 1 ;;
			"") [ "$default" = "default-y" ] && return 0; return 1 ;;
		esac
	done
}

ask_value() {
	# ask_value "question" "default value"
	local question=$1 default=${2:-} answer
	if [ "$ASSUME_YES" -eq 1 ] || [ ! -t 0 ]; then
		printf '%s' "$default"
		return 0
	fi
	printf '  %s [%s]: ' "$question" "$default" >&2
	read -r answer
	printf '%s' "${answer:-$default}"
}

has_cmd() { command -v "$1" >/dev/null 2>&1; }

version_of() {
	# Prints the first x.y.z found in the --version output of a command.
	"$@" --version 2>/dev/null | head -1 | grep -oE '[0-9]+(\.[0-9]+)+(\.[0-9]+)?' | head -1
}

version_ge() {
	# version_ge 3.19 3.21.1
	[ "$(printf '%s\n%s\n' "$1" "$2" | sort -V | head -1)" = "$1" ]
}

resolve_tool() {
	# resolve_tool <command> <human name> <version flag test> -> echoes the resolved command or empty
	local command_name=$1 label=$2
	if has_cmd "$command_name"; then
		printf '%s' "$(command -v "$command_name")"
		return 0
	fi
	if [ "$CHECK_ONLY" -eq 1 ] || [ ! -t 0 ]; then
		return 1
	fi

	warn "${label} (${command_name}) was not found in PATH" >&2
	if ! ask "Is ${label} installed in another location?" default-n; then
		return 1
	fi
	local provided
	provided=$(ask_value "Enter the full path to ${command_name}" "")
	if [ -z "$provided" ]; then
		return 1
	fi
	if [ -d "$provided" ]; then
		provided="${provided%/}/${command_name}"
	fi
	if [ ! -x "$provided" ]; then
		bad "${provided} is not an executable file" >&2
		return 1
	fi
	ok "${label}: ${provided}" >&2
	printf '%s' "$provided"
}

detect_pm() {
	for candidate in apt-get dnf pacman zypper apk brew; do
		if has_cmd "$candidate"; then
			printf '%s' "$candidate"
			return 0
		fi
	done
	return 1
}

pm_pick() {
	# pm_pick "apt-get:libpq5" "dnf:libpq" ... -> installs with the matching package manager
	local pm entry
	pm=$(detect_pm) || { warn "No package manager found; install manually: $*"; return 1; }
	for entry in "$@"; do
		if [ "${entry%%:*}" = "$pm" ]; then
			pm_install "${entry#*:}"
			return $?
		fi
	done
	warn "Unknown package name for ${pm}: $*"
	return 1
}

pm_install() {
	# pm_install <package...>
	local pm
	if ! pm=$(detect_pm); then
		warn "No package manager found; install manually: $*"
		return 1
	fi
	local -a command
	case "$pm" in
		apt-get) command=(apt-get install -y "$@") ;;
		dnf) command=(dnf install -y "$@") ;;
		pacman) command=(pacman -S --noconfirm "$@") ;;
		zypper) command=(zypper --non-interactive install "$@") ;;
		apk) command=(apk add "$@") ;;
		brew) command=(brew install "$@") ;;
		esac

	if [ "$ALLOW_INSTALL" -eq 0 ] || [ "$CHECK_ONLY" -eq 1 ]; then
		warn "Install: ${command[*]} (run manually)"
		return 1
	fi
	if [ "$ASSUME_YES" -eq 0 ] && [ -t 0 ]; then
		ask "Run: sudo ${command[*]} ?" default-y || return 1
	fi

	if [ "$pm" != "brew" ] && [ "$(id -u)" -ne 0 ]; then
		if has_cmd sudo; then
			sudo "${command[@]}"
		else
			warn "sudo is unavailable and you are not root; install manually: ${command[*]}"
			return 1
		fi
	else
		"${command[@]}"
	fi
}

generate_secret() {
	if has_cmd openssl; then
		openssl rand -hex 24
	else
		head -c 24 /dev/urandom | od -An -tx1 | tr -d ' \n'
	fi
}

write_env_file() {
	# Creates .env from .env.example with freshly generated passwords (never overwrites).
	local env_path="${REPO_ROOT}/.env"
	if [ -f "$env_path" ]; then
		ok ".env already exists; leaving it unchanged"
		return 0
	fi
	if [ ! -f "${REPO_ROOT}/.env.example" ]; then
		warn ".env.example was not found; .env was not created"
		return 1
	fi
	if [ "$CHECK_ONLY" -eq 1 ]; then
		warn ".env will be created from .env.example (passwords will be generated)"
		return 0
	fi

	local admin migrator app redis
	admin=$(generate_secret); migrator=$(generate_secret); app=$(generate_secret); redis=$(generate_secret)
	sed \
		-e "s|^OW_PG_ADMIN_PASSWORD=.*|OW_PG_ADMIN_PASSWORD=${admin}|" \
		-e "s|^OW_PG_MIGRATOR_PASSWORD=.*|OW_PG_MIGRATOR_PASSWORD=${migrator}|" \
		-e "s|^OW_PG_APP_PASSWORD=.*|OW_PG_APP_PASSWORD=${app}|" \
		-e "s|^OW_PG_CONNINFO=.*|OW_PG_CONNINFO=host=127.0.0.1 port=5432 dbname=openworld user=ow_app password=${app} sslmode=prefer connect_timeout=3|" \
		-e "s|^OW_REDIS_PASSWORD=.*|OW_REDIS_PASSWORD=${redis}|" \
		"${REPO_ROOT}/.env.example" > "$env_path"
	chmod 600 "$env_path"
	ok ".env created with generated passwords (chmod 600)"
}

detect_pg_lib() {
	if has_cmd ldconfig && ldconfig -p 2>/dev/null | grep -q 'libpq\.so'; then
		return 0
	fi
	for candidate in /usr/lib/x86_64-linux-gnu/libpq.so.5 /usr/lib64/libpq.so.5 /usr/local/lib/libpq.so /opt/homebrew/lib/libpq.dylib /usr/local/opt/libpq/lib/libpq.dylib; do
		[ -e "$candidate" ] && return 0
	done
	return 1
}

env_value() {
	# env_value NAME -> value from .env (empty when absent)
	[ -f "${REPO_ROOT}/.env" ] || return 0
	grep -E "^$1=" "${REPO_ROOT}/.env" | tail -1 | cut -d= -f2-
}

step_toolchain() {
	step "1/6 Compiler and build tools"

	if ! resolve_tool git Git >/dev/null; then
		if ! has_cmd git; then
			bad "git was not found (required for SDK submodules)"
			ask "Install git?" default-y && pm_install git
		fi
	else
		ok "git $(version_of git)"
	fi

	local clang_bin cmake_bin ninja_bin
	clang_bin=$(resolve_tool clang++ Clang) || clang_bin=""
	if [ -n "$clang_bin" ]; then
		local clang_version
		clang_version=$(version_of "$clang_bin")
		if [ -z "$clang_version" ]; then
			bad "clang++ was found but cannot run (${clang_bin}); check PATH and LD_LIBRARY_PATH"
		else
			ok "clang++ ${clang_version} -> ${clang_bin}"
			CLANGXX="$clang_bin"
		fi
	else
		bad "clang++ was not found; CMakeLists requires Clang for Linux builds"
		if ask "Install clang?" default-y; then
			pm_install clang
			has_cmd clang++ && CLANGXX=$(command -v clang++)
		fi
	fi

	cmake_bin=$(resolve_tool cmake CMake) || cmake_bin=""
	if [ -n "$cmake_bin" ]; then
		local cmake_version
		cmake_version=$(version_of "$cmake_bin")
		if [ -z "$cmake_version" ]; then
			bad "cmake was found but cannot run (${cmake_bin})"
		elif version_ge "3.19" "$cmake_version"; then
			ok "cmake ${cmake_version} -> ${cmake_bin}"
			CMAKE="$cmake_bin"
		else
			bad "cmake ${cmake_version} < 3.19"
			ask "Update cmake?" default-y && pm_install cmake
		fi
	else
		bad "cmake was not found"
		if ask "Install cmake?" default-y; then
			pm_install cmake
			has_cmd cmake && CMAKE=$(command -v cmake)
		fi
	fi

	ninja_bin=$(resolve_tool ninja Ninja) || ninja_bin=""
	if [ -n "$ninja_bin" ]; then
		ok "ninja $(version_of "$ninja_bin") -> ${ninja_bin}"
		NINJA="$ninja_bin"
	else
		warn "ninja was not found; the build will use Unix Makefiles (slower)"
		if ask "Install ninja?" default-y; then
			pm_pick "apt-get:ninja-build" "dnf:ninja-build" "pacman:ninja" "zypper:ninja" "apk:ninja" "brew:ninja"
			has_cmd ninja && NINJA=$(command -v ninja)
		fi
	fi
}

step_sdk() {
	step "2/6 SDK: hl2sdk-cs2 and metamod-source"
	if [ -d "${REPO_ROOT}/hl2sdk-cs2/game/server" ] && [ -d "${REPO_ROOT}/metamod-source/core" ]; then
		ok "Submodules are already available"
		return 0
	fi
	if [ "$CHECK_ONLY" -eq 1 ]; then
		warn "Submodules are not initialized (git submodule update --init --recursive)"
		return 0
	fi
	if ! ask "Initialize SDK submodules (git submodule update --init --recursive)?" default-y; then
		return 0
	fi
	if (cd "$REPO_ROOT" && git submodule update --init --recursive); then
		ok "Submodules are ready"
	elif (cd "$REPO_ROOT" && git config --unset submodule.hl2sdk-cs2.shallow; git config --unset submodule.metamod-source.shallow; 		git submodule update --init --recursive); then
		# .gitmodules marks the SDK submodules as shallow; when the pinned commit is no longer the
		# branch tip a shallow fetch cannot resolve it, so retry with full history.
		ok "Submodules are ready (shallow mode disabled to fetch the pinned commit)"
	else
		bad "Failed to initialize submodules (check access to github.com)"
	fi
}

step_postgres() {
	step "4/6 PostgreSQL (the plugin adapter connects through libpq)"
	if [ "$SKIP_POSTGRES" -eq 1 ]; then
		warn "Skipped by --skip-postgres"
		return 0
	fi

	if detect_pg_lib; then
		ok "libpq was found (required by the plugin to connect)"
	else
		bad "libpq was not found; the plugin cannot connect to PostgreSQL"
		if ask "Install the libpq client library?" default-y; then
			pm_pick "apt-get:libpq5" "dnf:libpq" "pacman:postgresql-libs" "zypper:postgresql-devel" "apk:postgresql-client" "brew:libpq"
			detect_pg_lib && ok "libpq installed"
		fi
	fi

	local psql_bin
	psql_bin=$(resolve_tool psql psql) || psql_bin=""

	if [ -n "$psql_bin" ]; then
		ok "psql $(version_of "$psql_bin") -> ${psql_bin}"
	elif ask "Do you have PostgreSQL installed?" default-n; then
		local provided
		provided=$(ask_value "Path to the PostgreSQL bin directory (for example /usr/lib/postgresql/17/bin)" "")
		if [ -n "$provided" ] && [ -x "${provided%/}/psql" ]; then
			psql_bin="${provided%/}/psql"
			ok "psql -> ${psql_bin}"
		else
			bad "psql was not found at the specified path"
		fi
	else
		warn "PostgreSQL was not found"
		if ask "Install the PostgreSQL server?" default-n; then
			if has_cmd docker; then
				if ask "Use docker compose (compose.yaml) instead of a system package?" default-y; then
					step_env
					(cd "$REPO_ROOT" && docker compose up -d postgres) && ok "PostgreSQL started in Docker"
				else
					pm_install postgresql
				fi
			else
				pm_install postgresql
			fi
			has_cmd psql && psql_bin=$(command -v psql)
		fi
	fi

	[ -z "$psql_bin" ] && return 0
	if ! ask "Create the roles and openworld database, then apply migrations now?" default-n; then
		NOTES+=("Database not initialized. Run database/bootstrap/001..003.sql and database/migrations/*.sql manually")
		return 0
	fi

	local admin_user admin_db host port
	admin_user=$(ask_value "PostgreSQL administrator" "postgres")
	admin_db=$(ask_value "Maintenance database" "postgres")
	host=$(ask_value "PostgreSQL host" "127.0.0.1")
	port=$(ask_value "PostgreSQL port" "5432")

	local admin_password
	admin_password=$(ask_value "Administrator password (empty to use peer/trust)" "")
	local -a psql_admin=( "$psql_bin" -h "$host" -p "$port" -U "$admin_user" -d "$admin_db" -v ON_ERROR_STOP=1 -q )
	run_admin() {
		if [ -n "$admin_password" ]; then
			PGPASSWORD="$admin_password" "${psql_admin[@]}" "$@"
		else
			"${psql_admin[@]}" "$@"
		fi
	}

	if ! run_admin -c 'SELECT 1' >/dev/null 2>&1; then
		bad "Could not connect to PostgreSQL as ${admin_user}"
		return 1
	fi

	run_admin -f "${REPO_ROOT}/database/bootstrap/001_create_roles_and_database.sql" || warn "001_create_roles_and_database.sql: see output above (roles may already exist)"
	run_admin -f "${REPO_ROOT}/database/bootstrap/002_create_database.sql" || warn "002_create_database.sql: the database may already exist"

	local migrator_password app_password
	migrator_password=$(env_value OW_PG_MIGRATOR_PASSWORD)
	app_password=$(env_value OW_PG_APP_PASSWORD)
	if [ -n "$migrator_password" ] && [ -n "$app_password" ]; then
		run_admin -c "ALTER ROLE ow_migrator PASSWORD '${migrator_password}'"
		run_admin -c "ALTER ROLE ow_app PASSWORD '${app_password}'"
		ok "Role passwords set from .env"
	else
		warn "Role passwords are not set; create .env first (step 5)"
	fi

	local migration
	for migration in $(ls "${REPO_ROOT}"/database/migrations/*.sql | sort); do
		if PGPASSWORD="$migrator_password" "$psql_bin" -h "$host" -p "$port" -U ow_migrator -d openworld -v ON_ERROR_STOP=1 -q -f "$migration"; then
			ok "migration $(basename "$migration")"
		else
			bad "Migration $(basename "$migration") failed"
			return 1
		fi
	done

	PGPASSWORD="$migrator_password" "$psql_bin" -h "$host" -p "$port" -U ow_migrator -d openworld -v ON_ERROR_STOP=1 -q \
		-f "${REPO_ROOT}/database/bootstrap/003_app_permissions.sql" && ok "Permissions granted to ow_app"
}

step_redis() {
	step "5/6 Redis (only for builds with OW_ENABLE_CACHE_FALLBACK=ON)"
	if [ "$SKIP_REDIS" -eq 1 ]; then
		warn "Skipped by --skip-redis"
		return 0
	fi

	if has_cmd redis-server || has_cmd redis-cli; then
		ok "Redis is already installed"
		if ! has_cmd redis-cli || ! redis-cli ping >/dev/null 2>&1; then
			warn "Redis does not respond to ping (is the service running?)"
		fi
		return 0
	fi

	if [ "$CHECK_ONLY" -eq 1 ]; then
		warn "Redis is not installed (only required for the cache fallback)"
		return 0
	fi
	if ! ask "Redis was not found. Install it? (only needed for the experimental cache fallback)" default-n; then
		ok "Redis is not required for the standard build"
		return 0
	fi

	if has_cmd docker; then
		if ask "Start Redis with docker compose (compose.yaml)?" default-y; then
			step_env
			(cd "$REPO_ROOT" && docker compose up -d redis) && ok "Redis started in Docker"
			return 0
		fi
	fi

	pm_pick "apt-get:redis-server" "dnf:redis" "pacman:redis" "zypper:redis" "apk:redis" "brew:redis"
	if has_cmd redis-server; then
		ok "Redis installed; set requirepass manually: /etc/redis/redis.conf -> requirepass <OW_REDIS_PASSWORD>"
		NOTES+=("Redis: set requirepass (the OW_REDIS_PASSWORD value in .env) and restart the service")
	fi
}

step_env() {
	step "3/6 Environment variables (.env)"
	if [ -f "${REPO_ROOT}/.env" ]; then
		ok ".env already exists"
		return 0
	fi
	write_env_file || true
}

step_build() {
	step "6/6 Build and tests"
	if [ "$SKIP_BUILD" -eq 1 ]; then
		warn "Skipped by --skip-build"
		return 0
	fi
	if [ -z "${CMAKE:-}" ] || [ -z "${CLANGXX:-}" ]; then
		bad "Cannot build: cmake or clang++ was not found"
		return 1
	fi
	if [ "$CHECK_ONLY" -eq 1 ]; then
		warn "Build not run (--check)"
		return 0
	fi
	if ! ask "Build the plugin and run tests now?" default-y; then
		return 0
	fi

	local -a generator=()
	[ -n "${NINJA:-}" ] && generator=(-G Ninja)
	local jobs
	jobs=$( (nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4) )
	[ "$jobs" -gt 8 ] && jobs=8

	if ! "$CMAKE" -S "$REPO_ROOT" -B "${REPO_ROOT}/${BUILD_DIR}" "${generator[@]}" \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_C_COMPILER="${CLANGXX%++}" \
		-DCMAKE_CXX_COMPILER="$CLANGXX"; then
		bad "CMake configuration failed"
		return 1
	fi
	if ! "$CMAKE" --build "${REPO_ROOT}/${BUILD_DIR}" -j "$jobs"; then
		bad "Build failed"
		return 1
	fi
	ok "Plugin built: ${BUILD_DIR}/package"

	local ctest_bin
	ctest_bin="$(dirname -- "$CMAKE")/ctest"
	[ -x "$ctest_bin" ] || ctest_bin=$(command -v ctest || true)
	if [ -n "$ctest_bin" ]; then
		if "$ctest_bin" --test-dir "${REPO_ROOT}/${BUILD_DIR}" --output-on-failure; then
			ok "Tests passed"
		else
			bad "Tests failed"
		fi
	else
		warn "ctest was not found; tests were not run"
	fi
}

summary() {
	step "Summary"
	if [ ${#MISSING[@]} -eq 0 ]; then
		ok "All required dependencies are available"
	else
		local item
		for item in "${MISSING[@]}"; do bad "$item"; done
	fi
	if [ ${#NOTES[@]} -gt 0 ]; then
		local note
		while IFS= read -r note; do warn "$note"; done < <(printf '%s\n' "${NOTES[@]}" | awk '!seen[$0]++')
	fi
	say ""
	say "Next steps:"
	say "  * Copy package/ -> addons/cs2-openworld on the server (see PACKAGE_INSTALL.txt)"
	say "  * Check gamedata: tools/update_offsets.py --check --binary <libserver.so>"
	say "  * Config: configs/openworld.ini.example -> addons/cs2-openworld/configs/openworld.ini"
	if [ ${#MISSING[@]} -gt 0 ]; then
		return 1
	fi
	return 0
}

main() {
	say "${BOLD}CS2 Open World - dependency installer${RESET}"
	say "Repository: ${REPO_ROOT}"
	if [ "$CHECK_ONLY" -eq 1 ]; then
		say "Mode: check only (no changes will be made)"
	fi

	local arch
	arch=$(uname -m)
	if [ "$arch" != "x86_64" ] && [ "$arch" != "amd64" ]; then
		bad "Unsupported architecture ${arch}: the plugin can only be built for x86_64"
	fi

	CLANGXX=""; CMAKE=""; NINJA=""
	step_toolchain
	step_sdk
	step_env
	step_postgres
	step_redis
	step_build
	summary
}

main "$@"
