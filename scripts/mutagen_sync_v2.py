#!/Volumes/code/00working/Asgard/github.pie/horizon-soc-fpga/.venv/bin/python
"""
Simplified Mutagen Sync Manager for FPGA Development
"""

import json
import subprocess
import sys
import time
from pathlib import Path
from typing import Optional

import click
import yaml
from loguru import logger
from rich.console import Console
from rich.table import Table
from rich import box


# Status display functions
def parse_sessions(output: str) -> list[dict]:
    """Parse mutagen sync list output into session dictionaries"""
    sessions = []
    current_session = {}
    lines = output.strip().split('\n')
    conflicts = []
    in_conflicts = False
    
    for line in lines:
        line = line.strip()
        if line.startswith('Name:'):
            if current_session:
                if conflicts:
                    current_session['conflicts'] = conflicts
                    conflicts = []
                sessions.append(current_session)
                in_conflicts = False
            current_session = {'name': line.split(':', 1)[1].strip()}
        elif line.startswith('Status:'):
            current_session['status'] = line.split(':', 1)[1].strip()
        elif line.startswith('URL:') and 'source' not in current_session:
            current_session['source'] = line.split(':', 1)[1].strip()
        elif line.startswith('URL:') and 'target' not in current_session:
            current_session['target'] = line.split(':', 1)[1].strip()
        elif 'files' in line and 'file_info' not in current_session:
            current_session['file_info'] = line.strip()
        elif 'Conflicts:' in line or 'conflicts:' in line.lower():
            in_conflicts = True
        elif in_conflicts and line and not line.startswith(('Name:', 'Status:', 'URL:')):
            # Capture conflict file paths
            if line.startswith(('- ', '* ', '• ')) or (line and not line.endswith(':')):
                conflict_path = line.lstrip('- *•').strip()
                if conflict_path:
                    conflicts.append(conflict_path)
    
    if current_session:
        if conflicts:
            current_session['conflicts'] = conflicts
        sessions.append(current_session)
    
    return sessions


def format_status(status: str, has_conflicts: bool = False) -> str:
    """Format status with color and icon"""
    status_lower = status.lower()
    if has_conflicts or 'conflict' in status_lower:
        return "[bold red]⚠ Conflicts[/bold red]"
    elif 'watching' in status_lower:
        return "[bold green]✓ Active[/bold green]"
    elif 'error' in status_lower:
        return "[bold red]✗ Error[/bold red]"
    elif 'scanning' in status_lower or 'staging' in status_lower:
        return "[bold cyan]↻ Syncing[/bold cyan]"
    elif 'paused' in status_lower:
        return "[bold yellow]⏸ Paused[/bold yellow]"
    else:
        return status


def create_status_table(sessions: list[dict], verbose: bool = False) -> Table:
    """Create a formatted table from sessions"""
    import re
    
    table = Table(title="Mutagen Sync Status", box=box.ROUNDED)
    table.add_column("Session", style="cyan")
    table.add_column("Local", style="green")
    table.add_column("Remote", style="yellow")
    table.add_column("Files", justify="right", style="blue")
    table.add_column("Status", justify="center")
    
    for session in sessions:
        name = session.get('name', 'Unknown')
        source = session.get('source', 'N/A')
        target = session.get('target', 'N/A')
        status = session.get('status', 'Unknown')
        
        # Format paths
        if verbose:
            # Show full paths in verbose mode
            local_path = source
            remote_path = target
        else:
            # Show abbreviated paths
            local_path = source.split('/')[-1] if '/' in source else source
            remote_parts = target.split(':') if ':' in target else [target]
            remote_host = remote_parts[0].split('@')[-1] if '@' in remote_parts[0] else remote_parts[0]
            remote_path = remote_host
        
        # Extract file count
        file_info = session.get('file_info', '')
        file_count = 'N/A'
        if 'files' in file_info:
            match = re.search(r'(\d+)\s+files', file_info)
            if match:
                file_count = match.group(1)
        
        # Format status
        has_conflicts = bool(session.get('conflicts'))
        status_display = format_status(status, has_conflicts)
        
        table.add_row(name, local_path, remote_path, file_count, status_display)
    
    return table


class MutagenSync:
    """Manages mutagen sync sessions with git integration"""
    
    def __init__(self, config_path: Optional[Path] = None, profile: str = "default", verbose: bool = False):
        self.config = self._load_config(config_path, profile)
        self.session_name = f"fpga-sync-{profile}"
        self.console = Console()
        self.verbose = verbose
        
    def _load_config(self, config_path: Optional[Path], profile: str) -> dict:
        """Load configuration from file or use defaults"""
        defaults = {
            'local_path': Path(__file__).parent.parent,
            'remote_host': 'rem@192.168.1.67',
            'remote_path': '/Volumes/ss990Pro2T/github.pie/horizon/horizon-soc-fpga',
            'profile': profile
        }
        
        # Search for config file
        if not config_path:
            search_paths = [
                Path(__file__).parent / "mutagen-sync.yml",
                Path.cwd() / ".mutagen-sync.yml",
            ]
            for path in search_paths:
                if path.exists():
                    config_path = path
                    break
        
        if config_path and config_path.exists():
            with open(config_path) as f:
                data = yaml.safe_load(f)
                if 'profiles' in data and profile in data['profiles']:
                    config = {**defaults, **data['profiles'][profile]}
                else:
                    config = {**defaults, **data}
                # Convert local_path to Path if it's a string
                if isinstance(config['local_path'], str):
                    config['local_path'] = Path(config['local_path'])
                logger.info(f"Loaded config from {config_path} (profile: {profile})")
                return config
        
        logger.info("Using default configuration")
        return defaults
    
    def _get_ignore_patterns(self) -> list[str]:
        """Get ignore patterns using git"""
        patterns = []
        
        # Check for .mutagenignore first
        mutagenignore = self.config['local_path'] / '.mutagenignore'
        if mutagenignore.exists():
            with open(mutagenignore) as f:
                patterns = [line.strip() for line in f 
                           if line.strip() and not line.startswith('#')]
            logger.info(f"Using .mutagenignore with {len(patterns)} patterns")
            return patterns
        
        # Use git to find what should be ignored
        try:
            # Get all ignored files
            result = subprocess.run(
                ['git', 'ls-files', '--others', '--ignored', '--exclude-standard'],
                cwd=self.config['local_path'],
                capture_output=True,
                text=True,
                check=True
            )
            
            if result.stdout.strip():
                # Extract patterns from ignored files
                ignored_files = result.stdout.strip().split('\n')
                seen_dirs = set()
                seen_extensions = set()
                
                for path in ignored_files:
                    parts = path.split('/')
                    
                    # Add directory patterns
                    if parts[0] not in seen_dirs:
                        if parts[0] in ['.venv', '__pycache__', '.git', 'build', 'dist']:
                            patterns.append(f"{parts[0]}/**")
                            seen_dirs.add(parts[0])
                    
                    # Add extension patterns
                    if '.' in parts[-1]:
                        ext = parts[-1].split('.')[-1]
                        if ext in ['pyc', 'pyo', 'so'] and f"*.{ext}" not in seen_extensions:
                            patterns.append(f"*.{ext}")
                            seen_extensions.add(f"*.{ext}")
                    
                    # Handle files with commas - add as exact paths
                    if ',' in parts[-1]:
                        patterns.append(path)
            
            # Essential patterns
            patterns.extend(['.git/**', '.env'])
            
        except subprocess.CalledProcessError:
            # Fallback patterns if not in git repo
            patterns = ['.git/**', '.venv/**', '__pycache__/**', '*.pyc', '.env']
        
        return list(set(patterns))  # Remove duplicates
    
    def _run_mutagen(self, *args) -> subprocess.CompletedProcess:
        """Run a mutagen command"""
        cmd = ['mutagen'] + list(args)
        return subprocess.run(cmd, capture_output=True, text=True)
    
    def _ssh_test(self) -> bool:
        """Test SSH connection"""
        result = subprocess.run(
            ['ssh', '-o', 'ConnectTimeout=5', '-o', 'BatchMode=yes', 
             self.config['remote_host'], 'echo', 'ok'],
            capture_output=True
        )
        return result.returncode == 0
    
    def start(self, force: bool = False):
        """Start sync session"""
        # Check SSH
        if not self._ssh_test():
            raise click.ClickException(f"Cannot connect to {self.config['remote_host']}")
        
        # Check if session exists
        result = self._run_mutagen('sync', 'list')
        if result.stdout and self.session_name in result.stdout:
            if not force:
                click.echo(f"Session {self.session_name} already exists. Use --force to recreate.")
                return
            else:
                self.stop()
        
        # Get ignore patterns
        ignore_args = []
        for pattern in self._get_ignore_patterns():
            ignore_args.extend(['--ignore', pattern])
        
        # Create session
        cmd = [
            'mutagen', 'sync', 'create',
            f'--name={self.session_name}',
            *ignore_args,
            str(self.config['local_path']),
            f"{self.config['remote_host']}:{self.config['remote_path']}"
        ]
        
        logger.debug(f"Creating sync with {len(ignore_args)//2} ignore patterns")
        result = subprocess.run(cmd, capture_output=True, text=True)
        
        if result.returncode == 0:
            click.secho(f"✓ Started sync session: {self.session_name}", fg='green')
            self.status()
        else:
            raise click.ClickException(f"Failed to create sync: {result.stderr}")
    
    def stop(self, all_sessions: bool = False):
        """Stop sync session(s)"""
        if all_sessions:
            # Get all fpga-sync-* sessions by parsing list output
            result = self._run_mutagen('sync', 'list')
            if result.stdout:
                stopped = []
                lines = result.stdout.strip().split('\n')
                
                for line in lines:
                    if line.strip().startswith('Name:'):
                        name = line.split(':', 1)[1].strip()
                        if name.startswith('fpga-sync-'):
                            term_result = self._run_mutagen('sync', 'terminate', name)
                            if term_result.returncode == 0:
                                stopped.append(name)
                
                if stopped:
                    click.secho(f"✓ Stopped {len(stopped)} sync session(s): {', '.join(stopped)}", fg='green')
                else:
                    click.secho("No fpga-sync sessions found", fg='yellow')
            else:
                click.secho("No active sessions", fg='yellow')
        else:
            result = self._run_mutagen('sync', 'terminate', self.session_name)
            if result.returncode == 0:
                click.secho(f"✓ Stopped sync session: {self.session_name}", fg='green')
            else:
                click.secho(f"Session {self.session_name} not found", fg='yellow')
    
    def restart(self, force: bool = False):
        """Restart sync session"""
        self.stop()
        time.sleep(1)
        self.start(force)
    
    def status(self, watch: bool = False, verbose: bool = None):
        """Show sync status"""
        # Use local verbose if provided, otherwise fall back to instance verbose
        use_verbose = verbose if verbose is not None else self.verbose
        
        def show():
            result = self._run_mutagen('sync', 'list')
            if result.returncode != 0 or not result.stdout.strip():
                click.secho("No active sync sessions", fg='yellow')
                return
            
            # Parse sessions
            sessions = parse_sessions(result.stdout)
            
            # Filter fpga-sync sessions
            fpga_sessions = [s for s in sessions if s.get('name', '').startswith('fpga-sync-')]
            
            if not fpga_sessions:
                click.secho("No fpga-sync sessions found", fg='yellow')
                return
            
            # Create and display table
            table = create_status_table(fpga_sessions, verbose=use_verbose)
            self.console.print(table)
            
            # Show conflicts if any
            for session in fpga_sessions:
                conflicts = session.get('conflicts', [])
                if conflicts:
                    self.console.print(f"\n[bold red]⚠ Conflicts in {session['name']}:[/bold red]")
                    source_path = session.get('source', '')
                    for conflict in conflicts:
                        full_path = f"{source_path}/{conflict}" if source_path else conflict
                        self.console.print(f"  [red]→ {full_path}[/red]")
        
        if watch:
            try:
                while True:
                    click.clear()
                    show()
                    click.echo("\nPress Ctrl+C to exit")
                    time.sleep(2)
            except KeyboardInterrupt:
                pass
        else:
            show()


# CLI commands
@click.group()
@click.option('--config', '-c', type=click.Path(exists=True))
@click.option('--profile', '-p', default='default')
@click.option('--verbose', '-v', is_flag=True)
@click.pass_context
def cli(ctx, config, profile, verbose):
    """Simplified Mutagen Sync Manager"""
    if verbose:
        logger.remove()
        logger.add(sys.stderr, level="DEBUG")
    else:
        logger.remove()
        logger.add(sys.stderr, level="INFO", format="{message}")
    
    ctx.obj = MutagenSync(Path(config) if config else None, profile, verbose=verbose)


@cli.command()
@click.option('--force', '-f', is_flag=True, help='Force recreate session')
@click.pass_obj
def start(sync, force):
    """Start sync session"""
    sync.start(force)


@cli.command()
@click.option('--all', '-a', is_flag=True, help='Stop all fpga-sync sessions')
@click.pass_obj
def stop(sync, all):
    """Stop sync session"""
    sync.stop(all_sessions=all)


@cli.command()
@click.option('--force', '-f', is_flag=True)
@click.pass_obj
def restart(sync, force):
    """Restart sync session"""
    sync.restart(force)


@cli.command()
@click.option('--watch', '-w', is_flag=True, help='Watch status')
@click.option('--verbose', '-v', is_flag=True, help='Show full paths')
@click.pass_obj
def status(sync, watch, verbose):
    """Show sync status"""
    sync.status(watch, verbose)


@cli.command()
def init():
    """Create example config file"""
    example = {
        'profiles': {
            'default': {
                'local_path': str(Path.cwd()),
                'remote_host': 'user@hostname',
                'remote_path': '/path/to/remote/project'
            }
        }
    }
    
    path = Path('mutagen-sync.yml')
    if path.exists():
        if not click.confirm(f"{path} exists. Overwrite?"):
            return
    
    with open(path, 'w') as f:
        yaml.dump(example, f, default_flow_style=False)
    
    click.secho(f"✓ Created {path}", fg='green')


if __name__ == '__main__':
    cli()