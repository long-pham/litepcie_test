#!/usr/bin/env python3
"""
Improved Mutagen Sync Manager for FPGA Development

This script manages bidirectional file synchronization between local and remote
development environments using Mutagen.
"""

import json
import shlex
import subprocess
import sys
import time
from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path
from typing import Any

import click
import yaml
from loguru import logger

# Configure logger to avoid duplicate messages
logger.remove()  # Remove default handler


class SyncDirection(Enum):
    """Enumeration for sync directions"""
    PUSH = "push"
    PULL = "pull"
    BOTH = "both"


@dataclass
class SyncConfig:
    """Configuration for sync operations"""
    local_path: Path
    remote_host: str
    remote_path: str
    direction: SyncDirection = SyncDirection.PUSH
    ssh_timeout: int = 10
    retry_count: int = 3
    retry_delay: int = 2
    ignore_patterns: list[str] = field(default_factory=list)
    profile_name: str = "default"

    def __post_init__(self):
        """Validate and normalize paths"""
        if isinstance(self.local_path, str):
            self.local_path = Path(self.local_path).resolve()
        if isinstance(self.direction, str):
            self.direction = SyncDirection(self.direction)

        # Validate remote path format
        if not self.remote_path or self.remote_path.strip() != self.remote_path:
            raise ValueError(f"Invalid remote path: '{self.remote_path}'")

        # Sanitize remote path
        self.remote_path = shlex.quote(self.remote_path)


class ConfigManager:
    """Manages configuration loading and validation"""

    CONFIG_SEARCH_PATHS = [
        Path.cwd() / ".mutagen-sync.yml",
        Path.cwd() / "mutagen-sync.yml",
        Path.home() / ".config" / "mutagen" / "sync.yml",
        Path.home() / ".mutagen-sync.yml"
    ]

    @classmethod
    def find_config_file(cls) -> Path | None:
        """Search for configuration file in standard locations"""
        for path in cls.CONFIG_SEARCH_PATHS:
            if path.exists():
                logger.debug(f"Found config file at {path}")
                return path
        return None

    @classmethod
    def load_config(cls, config_path: Path | None = None, profile: str = "default") -> SyncConfig:
        """Load configuration from file or use defaults"""
        script_dir = Path(__file__).resolve().parent

        # Default configuration
        defaults = {
            'local_path': script_dir.parent,
            'remote_host': 'rem@192.168.1.67',
            'remote_path': '/Volumes/ss990Pro2T/github.pie/mshw-fpga-workingdrop2.0/AsgardEmu3',
            'direction': 'push',
            'ssh_timeout': 10,
            'retry_count': 3,
            'retry_delay': 2
        }

        if not config_path:
            config_path = cls.find_config_file()

        if config_path and config_path.exists():
            try:
                with open(config_path) as f:
                    config_data = yaml.safe_load(f)

                # Support for multiple profiles
                if 'profiles' in config_data:
                    if profile not in config_data['profiles']:
                        raise ValueError(f"Profile '{profile}' not found in config")
                    profile_config = config_data['profiles'][profile]
                else:
                    profile_config = config_data

                # Merge with defaults
                config_dict = {**defaults, **profile_config}
                config_dict['profile_name'] = profile

                logger.info(f"Loaded configuration from {config_path} (profile: {profile})")
                return SyncConfig(**config_dict)

            except Exception as e:
                logger.error(f"Failed to load config from {config_path}: {e}")
                raise

        logger.info("Using default configuration")
        return SyncConfig(**defaults)

    @classmethod
    def save_example_config(cls, path: Path):
        """Save an example configuration file"""
        example_config = {
            'profiles': {
                'default': {
                    'local_path': str(Path.cwd()),
                    'remote_host': 'user@hostname',
                    'remote_path': '/path/to/remote/project',
                    'direction': 'push',
                    'ssh_timeout': 10,
                    'retry_count': 3,
                    'ignore_patterns': [
                        '.git/',
                        '.venv/',
                        '__pycache__/',
                        '*.pyc'
                    ]
                },
                'development': {
                    'local_path': str(Path.cwd()),
                    'remote_host': 'dev@dev-server',
                    'remote_path': '/home/dev/project',
                    'direction': 'both'
                }
            }
        }

        with open(path, 'w') as f:
            yaml.dump(example_config, f, default_flow_style=False)

        logger.info(f"Saved example configuration to {path}")


class SSHConnection:
    """Manages SSH connectivity and validation"""

    @staticmethod
    def test_connection(host: str, timeout: int = 10) -> tuple[bool, str]:
        """Test SSH connection to host"""
        try:
            cmd = ['ssh', '-o', f'ConnectTimeout={timeout}', '-o', 'BatchMode=yes',
                   host, 'echo', 'connected']
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout + 5)

            if result.returncode == 0:
                return True, "Connection successful"
            else:
                return False, f"SSH connection failed: {result.stderr.strip()}"

        except subprocess.TimeoutExpired:
            return False, f"SSH connection timed out after {timeout} seconds"
        except Exception as e:
            return False, f"SSH connection error: {str(e)}"

    @staticmethod
    def validate_remote_path(host: str, path: str, timeout: int = 10, create_if_missing: bool = True) -> tuple[bool, str]:
        """Validate that remote path exists, optionally creating it if missing"""
        try:
            # First check if path exists
            cmd = ['ssh', '-o', f'ConnectTimeout={timeout}', host, 'test', '-d', path]
            result = subprocess.run(cmd, capture_output=True, timeout=timeout + 5)

            if result.returncode == 0:
                return True, "Remote path exists"

            # Path doesn't exist
            if create_if_missing:
                # Try to create the directory
                logger.info(f"Remote path '{path}' does not exist, attempting to create it...")
                mkdir_cmd = ['ssh', '-o', f'ConnectTimeout={timeout}', host, 'mkdir', '-p', path]
                mkdir_result = subprocess.run(mkdir_cmd, capture_output=True, text=True, timeout=timeout + 5)

                if mkdir_result.returncode == 0:
                    logger.info(f"Successfully created remote path: {path}")
                    return True, f"Remote path created: {path}"
                else:
                    error_msg = mkdir_result.stderr.strip() or "Unknown error"
                    return False, f"Failed to create remote path '{path}': {error_msg}"
            else:
                return False, f"Remote path '{path}' does not exist"

        except subprocess.TimeoutExpired:
            return False, f"Path validation timed out after {timeout} seconds"
        except Exception as e:
            return False, f"Path validation error: {str(e)}"


class GitignoreParser:
    """Parses and processes .gitignore patterns"""

    def __init__(self, base_path: Path):
        self.base_path = base_path
        self._patterns_cache = None

    def read_patterns(self) -> list[str]:
        """Read and parse .gitignore patterns"""
        if self._patterns_cache is not None:
            return self._patterns_cache

        patterns = ['.venv/**', '__pycache__/**', '*.pyc']  # Default patterns
        gitignore_path = self.base_path / '.gitignore'

        if gitignore_path.exists():
            try:
                with open(gitignore_path) as f:
                    for line in f:
                        # Remove comments and whitespace
                        line = line.split('#')[0].strip()
                        if line and not line.startswith('#'):
                            patterns.append(line)

                logger.debug(f"Loaded {len(patterns)} patterns from .gitignore")
            except Exception as e:
                logger.warning(f"Failed to read .gitignore: {e}")

        self._patterns_cache = patterns
        return patterns

    def convert_to_mutagen_patterns(self, patterns: list[str],
                                    direction: SyncDirection) -> list[str]:
        """Convert gitignore patterns to mutagen ignore arguments"""
        ignore_args = []

        # Patterns to skip based on direction
        skip_patterns = {
            SyncDirection.PULL: {'build/', 'dist/', '*.egg-info/'},
            SyncDirection.PUSH: set(),
            SyncDirection.BOTH: set()
        }

        for pattern in patterns:
            # Skip specific patterns based on direction
            if pattern in skip_patterns.get(direction, set()):
                continue

            # Skip binary files we want to sync
            if pattern in ['*.bit', '*.bin', 'analyzer.csv']:
                continue

            # Format pattern for mutagen
            if pattern.endswith('/'):
                pattern = f"{pattern}**"
            elif pattern.startswith('/'):
                pattern = pattern[1:]

            # Skip negation patterns
            if not pattern.startswith('!'):
                ignore_args.extend(['--ignore', pattern])

        return ignore_args


class MutagenSync:
    """Main sync manager class"""

    def __init__(self, config: SyncConfig):
        self.config = config
        self.ssh = SSHConnection()
        self.gitignore_parser = GitignoreParser(config.local_path)
        self._session_prefix = "fpga-sync"

    def validate_environment(self, create_remote_dir: bool = True) -> None:
        """Validate local and remote environments"""
        # Check local path
        if not self.config.local_path.exists():
            raise click.ClickException(f"Local path {self.config.local_path} does not exist")

        # Test SSH connection with retry
        for attempt in range(self.config.retry_count):
            success, message = self.ssh.test_connection(
                self.config.remote_host,
                self.config.ssh_timeout
            )
            if success:
                logger.info("SSH connection validated")
                break

            if attempt < self.config.retry_count - 1:
                logger.warning(f"SSH connection attempt {attempt + 1} failed: {message}")
                time.sleep(self.config.retry_delay)
            else:
                raise click.ClickException(f"Failed to connect after {self.config.retry_count} attempts: {message}")

        # Validate remote path
        success, message = self.ssh.validate_remote_path(
            self.config.remote_host,
            self.config.remote_path,
            self.config.ssh_timeout,
            create_if_missing=create_remote_dir
        )
        if not success:
            raise click.ClickException(message)

        logger.info("Environment validation successful")

    def check_mutagen_installed(self) -> None:
        """Check if mutagen is installed"""
        try:
            subprocess.run(['mutagen', 'version'], capture_output=True, check=True)
        except (subprocess.CalledProcessError, FileNotFoundError):
            raise click.ClickException(
                "Mutagen is not installed. Please install it from: https://mutagen.io/documentation/introduction/installation"
            ) from None

    def start_daemon(self) -> None:
        """Start mutagen daemon if not running"""
        try:
            subprocess.run(['mutagen', 'daemon', 'start'], capture_output=True, check=True)
            logger.debug("Mutagen daemon started")
        except subprocess.CalledProcessError as e:
            if b"already running" not in e.stderr:
                raise click.ClickException(f"Failed to start mutagen daemon: {e.stderr.decode()}") from e

    def get_session_name(self, direction: str) -> str:
        """Generate session name"""
        return f"{self._session_prefix}-{self.config.profile_name}-{direction}"

    def get_existing_sessions(self) -> dict[str, Any]:
        """Get information about existing sync sessions"""
        try:
            # First check if daemon is running
            daemon_check = subprocess.run(
                ['mutagen', 'daemon', 'status'],
                capture_output=True,
                check=False
            )

            if daemon_check.returncode != 0:
                logger.debug("Mutagen daemon not running")
                return {}

            result = subprocess.run(
                ['mutagen', 'sync', 'list', '--json'],
                capture_output=True,
                text=True,
                check=True
            )

            if not result.stdout.strip():
                return {}

            data = json.loads(result.stdout)

            # Extract session names
            sessions = {}
            if isinstance(data, dict):
                for _key, value in data.items():
                    if isinstance(value, dict) and 'name' in value:
                        sessions[value['name']] = value

            return sessions

        except (subprocess.CalledProcessError, json.JSONDecodeError) as e:
            logger.debug(f"Failed to get existing sessions: {e}")
            return {}

    def create_sync_session(self, direction: SyncDirection, dry_run: bool = False) -> None:
        """Create a sync session"""
        session_name = self.get_session_name(direction.value)

        # Get ignore patterns
        patterns = self.gitignore_parser.read_patterns()
        if self.config.ignore_patterns:
            patterns.extend(self.config.ignore_patterns)

        ignore_args = self.gitignore_parser.convert_to_mutagen_patterns(patterns, direction)

        # Build command
        if direction == SyncDirection.PUSH:
            alpha = str(self.config.local_path)
            beta = f"{self.config.remote_host}:{self.config.remote_path}"
        else:  # PULL
            alpha = f"{self.config.remote_host}:{self.config.remote_path}"
            beta = str(self.config.local_path)

        cmd = [
            'mutagen', 'sync', 'create',
            f'--name={session_name}',
            *ignore_args,
            alpha,
            beta
        ]

        if dry_run:
            click.echo(f"Would run: {' '.join(cmd)}")
            return

        try:
            logger.info(f"Creating {direction.value} sync session: {session_name}")
            subprocess.run(cmd, check=True)
            click.secho(f"✓ Created {direction.value} sync session", fg='green')
        except subprocess.CalledProcessError as e:
            raise click.ClickException(f"Failed to create sync session: {e}") from e

    def start_sync(self, direction: SyncDirection | None = None,
                   force: bool = False, dry_run: bool = False, create_remote_dir: bool = True) -> None:
        """Start sync sessions"""
        direction = direction or self.config.direction

        # Validate environment
        self.validate_environment(create_remote_dir=create_remote_dir)
        self.check_mutagen_installed()

        if not dry_run:
            self.start_daemon()

        existing_sessions = self.get_existing_sessions()

        # Handle different directions
        directions_to_create = []
        if direction == SyncDirection.BOTH:
            directions_to_create = [SyncDirection.PUSH, SyncDirection.PULL]
        else:
            directions_to_create = [direction]

        for dir_to_create in directions_to_create:
            session_name = self.get_session_name(dir_to_create.value)

            if session_name in existing_sessions and not force:
                click.secho(f"Session {session_name} already exists. Use --force to recreate.", fg='yellow')
                continue

            if session_name in existing_sessions and force:
                self.terminate_session(session_name)

            self.create_sync_session(dir_to_create, dry_run)

        if not dry_run:
            self.show_status()

    def stop_sync(self, direction: SyncDirection | None = None) -> None:
        """Stop sync sessions"""
        direction = direction or self.config.direction

        sessions_to_stop = []
        if direction == SyncDirection.BOTH:
            sessions_to_stop = [
                self.get_session_name("push"),
                self.get_session_name("pull")
            ]
        else:
            sessions_to_stop = [self.get_session_name(direction.value)]

        for session_name in sessions_to_stop:
            self.terminate_session(session_name)

        click.secho("✓ Stopped sync sessions", fg='green')

    def terminate_session(self, session_name: str) -> None:
        """Terminate a specific session"""
        try:
            subprocess.run(
                ['mutagen', 'sync', 'terminate', session_name],
                check=False,
                capture_output=True
            )
            logger.info(f"Terminated session: {session_name}")
        except Exception as e:
            logger.warning(f"Failed to terminate session {session_name}: {e}")

    def pause_sync(self, direction: SyncDirection | None = None) -> None:
        """Pause sync sessions"""
        direction = direction or self.config.direction

        sessions_to_pause = []
        if direction == SyncDirection.BOTH:
            sessions_to_pause = [
                self.get_session_name("push"),
                self.get_session_name("pull")
            ]
        else:
            sessions_to_pause = [self.get_session_name(direction.value)]

        for session_name in sessions_to_pause:
            try:
                subprocess.run(
                    ['mutagen', 'sync', 'pause', session_name],
                    check=True,
                    capture_output=True
                )
                click.secho(f"✓ Paused {session_name}", fg='green')
            except subprocess.CalledProcessError:
                click.secho(f"✗ Failed to pause {session_name}", fg='red')

    def resume_sync(self, direction: SyncDirection | None = None) -> None:
        """Resume sync sessions"""
        direction = direction or self.config.direction

        sessions_to_resume = []
        if direction == SyncDirection.BOTH:
            sessions_to_resume = [
                self.get_session_name("push"),
                self.get_session_name("pull")
            ]
        else:
            sessions_to_resume = [self.get_session_name(direction.value)]

        for session_name in sessions_to_resume:
            try:
                subprocess.run(
                    ['mutagen', 'sync', 'resume', session_name],
                    check=True,
                    capture_output=True
                )
                click.secho(f"✓ Resumed {session_name}", fg='green')
            except subprocess.CalledProcessError:
                click.secho(f"✗ Failed to resume {session_name}", fg='red')

    def show_status(self, watch: bool = False) -> None:
        """Show sync status"""
        if watch:
            try:
                while True:
                    click.clear()
                    click.secho(f"=== Mutagen Sync Status (Profile: {self.config.profile_name}) ===",
                                fg='blue', bold=True)
                    subprocess.run(['mutagen', 'sync', 'list'])
                    click.secho("\nPress Ctrl+C to exit watch mode", fg='yellow')
                    time.sleep(2)
            except KeyboardInterrupt:
                click.echo("\nExiting watch mode")
        else:
            click.secho(f"=== Mutagen Sync Status (Profile: {self.config.profile_name}) ===",
                        fg='blue', bold=True)
            subprocess.run(['mutagen', 'sync', 'list'])

    def monitor_sync(self, session_name: str) -> None:
        """Monitor a specific sync session"""
        try:
            subprocess.run(['mutagen', 'sync', 'monitor', session_name])
        except KeyboardInterrupt:
            click.echo("\nStopped monitoring")


# CLI Commands
@click.group()
@click.option('--config', '-c', type=click.Path(exists=True),
              help='Path to configuration file')
@click.option('--profile', '-p', default='default',
              help='Configuration profile to use')
@click.option('--verbose', '-v', is_flag=True,
              help='Enable verbose logging')
@click.pass_context
def cli(ctx, config, profile, verbose):
    """Improved Mutagen Sync Manager for FPGA Development"""
    # Configure logging
    if verbose:
        logger.add(sys.stderr, level="DEBUG", format="{time:HH:mm:ss} | {level} | {message}")
    else:
        logger.add(sys.stderr, level="INFO", format="{message}")

    # Load configuration
    config_path = Path(config) if config else None
    ctx.obj = ConfigManager.load_config(config_path, profile)


@cli.command()
@click.option('--local-path', '-l', type=click.Path(exists=True),
              help='Local project path')
@click.option('--remote-host', '-h',
              help='Remote host (user@hostname)')
@click.option('--remote-path', '-r',
              help='Remote project path')
@click.option('--direction', '-d',
              type=click.Choice(['push', 'pull', 'both']),
              help='Sync direction')
@click.option('--force', '-f', is_flag=True,
              help='Force recreate existing sessions')
@click.option('--dry-run', is_flag=True,
              help='Show what would be done without doing it')
@click.option('--no-create-remote', is_flag=True,
              help='Do not create remote directory if it does not exist')
@click.pass_obj
def start(config, local_path, remote_host, remote_path, direction, force, dry_run, no_create_remote):
    """Start sync session(s)"""
    # Override config with CLI options
    if local_path:
        config.local_path = Path(local_path).resolve()
    if remote_host:
        config.remote_host = remote_host
    if remote_path:
        config.remote_path = remote_path
    if direction:
        config.direction = SyncDirection(direction)

    sync_manager = MutagenSync(config)
    sync_manager.start_sync(force=force, dry_run=dry_run, create_remote_dir=not no_create_remote)


@cli.command()
@click.option('--direction', '-d',
              type=click.Choice(['push', 'pull', 'both']),
              help='Sync direction to stop')
@click.pass_obj
def stop(config, direction):
    """Stop sync session(s)"""
    if direction:
        config.direction = SyncDirection(direction)

    sync_manager = MutagenSync(config)
    sync_manager.stop_sync()


@cli.command()
@click.option('--direction', '-d',
              type=click.Choice(['push', 'pull', 'both']),
              help='Sync direction to restart')
@click.option('--force', '-f', is_flag=True,
              help='Force recreate sessions')
@click.pass_obj
def restart(config, direction, force):
    """Restart sync session(s)"""
    if direction:
        config.direction = SyncDirection(direction)

    sync_manager = MutagenSync(config)
    sync_manager.stop_sync()
    time.sleep(1)  # Brief pause between stop and start
    sync_manager.start_sync(force=force)


@cli.command()
@click.option('--watch', '-w', is_flag=True,
              help='Continuously watch status')
@click.pass_obj
def status(config, watch):
    """Show sync status"""
    sync_manager = MutagenSync(config)
    sync_manager.show_status(watch=watch)


@cli.command()
@click.option('--direction', '-d',
              type=click.Choice(['push', 'pull', 'both']),
              help='Sync direction to pause')
@click.pass_obj
def pause(config, direction):
    """Pause sync session(s)"""
    if direction:
        config.direction = SyncDirection(direction)

    sync_manager = MutagenSync(config)
    sync_manager.pause_sync()


@cli.command()
@click.option('--direction', '-d',
              type=click.Choice(['push', 'pull', 'both']),
              help='Sync direction to resume')
@click.pass_obj
def resume(config, direction):
    """Resume sync session(s)"""
    if direction:
        config.direction = SyncDirection(direction)

    sync_manager = MutagenSync(config)
    sync_manager.resume_sync()


@cli.command()
@click.option('--session', '-s', required=True,
              help='Session name to monitor')
@click.pass_obj
def monitor(config, session):
    """Monitor a specific sync session"""
    sync_manager = MutagenSync(config)
    full_session_name = f"{sync_manager._session_prefix}-{config.profile_name}-{session}"
    sync_manager.monitor_sync(full_session_name)


@cli.command()
@click.option('--output', '-o', type=click.Path(),
              default='mutagen-sync.yml',
              help='Output file path')
def init(output):
    """Initialize example configuration file"""
    output_path = Path(output)
    if output_path.exists():
        if not click.confirm(f"{output} already exists. Overwrite?"):
            return

    ConfigManager.save_example_config(output_path)
    click.secho(f"✓ Created example configuration at {output}", fg='green')


@cli.command()
@click.pass_obj
def info(config):
    """Show current configuration"""
    click.secho("=== Current Configuration ===", fg='blue', bold=True)
    click.echo(f"Profile: {config.profile_name}")
    click.echo(f"Local Path: {config.local_path}")
    click.echo(f"Remote Host: {config.remote_host}")
    click.echo(f"Remote Path: {config.remote_path}")
    click.echo(f"Direction: {config.direction.value}")
    click.echo(f"SSH Timeout: {config.ssh_timeout}s")
    click.echo(f"Retry Count: {config.retry_count}")

    # Show gitignore patterns
    parser = GitignoreParser(config.local_path)
    patterns = parser.read_patterns()
    click.echo(f"\nIgnore Patterns ({len(patterns)} total):")
    for pattern in patterns[:10]:  # Show first 10
        click.echo(f"  - {pattern}")
    if len(patterns) > 10:
        click.echo(f"  ... and {len(patterns) - 10} more")


if __name__ == '__main__':
    cli()
