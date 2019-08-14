"""Methods to extract version information from a git repository."""
import subprocess
import collections


def _get_command_output(command):
    """ Compatibility wrapper for grabbing process output across multiple python versions. """

    # python2.7 versions lack _get_command_output, so we'll emulate it by using check_output
    # and ignoring the process' return status
    try:
        output = subprocess.check_output(command, shell=True)
    except subprocess.CalledProcessError as e:
        output = e.output

    return output.strip()


def get_tag():
    """Return the last tag for the git repository reachable from HEAD."""
    # another possible option is: 'git tag --merged | sort -V | tail -n1'
    return _get_command_output("git tag --sort=version:refname --merged | tail -n1")


def get_tag_commit_sha(tag):
    """Return the commit that the tag is pointing to."""
    return _get_command_output("git rev-list -n 1 {tag}".format(tag=tag))


def is_dirty():
    """Return True or False depending on whether the working tree is dirty (considers untracked files as well)"""
    return len(_get_command_output("git status -s")) != 0


def is_head_at_tag(tag):
    """Return True or False depending on whether the given tag is pointing to HEAD"""
    return get_head_sha() == get_tag_commit_sha(tag)


def get_head_sha():
    """Return the sha key of HEAD."""
    return _get_command_output('git rev-parse HEAD')


def get_version(template="{tag}.dev{sha}", starting_version="0.1.0"):
    """
    Return the full git version using the given template. If there are no annotated tags, the version specified by
    starting_version will be used. If HEAD is at the tag, the version will be the tag itself. If there are commits ahead
    of the tag, the first 8 characters of the sha of the HEAD commit will be included.

    In all of the above cases, if the working tree is also dirty or contains untracked files, a "+dirty" suffix will be
    appended to the version.

    Args:
        template: the string format template to use. It can use these keys:
            {tag}: the tag from the git repository
            {sha}: first 8 characters of the sha key of HEAD
        starting_version: the starting version to use if there are no existing tags.

    Returns:
        the formatted version based on tags in the git repository.

    """

    tag = get_tag()
    if len(tag) == 0:
        version = starting_version
    elif is_head_at_tag(tag):
        version = tag
    else:
        sha = get_head_sha()[:8]
        version = template.format(tag=tag, sha=sha)

    if is_dirty():
        separator = '.' if ('+' in version) else '+'
        version = "{version}{separator}dirty".format(version=version, separator=separator)

    return version


def validate_version_config(dist, _, config):
    """Validate the `version_config` keyword in a client setup.py script."""
    if not isinstance(config, collections.Mapping):
        raise TypeError("Config should be a dictionary with `version_format` and `starting_version` keys.")

    if "starting_version" not in config:
        starting_version = "0.1.0"
    else:
        starting_version = config["starting_version"]

    if "version_format" not in config:
        template = "{tag}.dev{sha}"
    else:
        template = config["version_format"]

    dist.metadata.version = get_version(template, starting_version)


# explicitly define the outward facing API of this module
__all__ = [
    get_tag.__name__,
    get_tag_commit_sha.__name__,
    is_dirty.__name__,
    is_head_at_tag.__name__,
    get_head_sha.__name__,
    get_version.__name__,
    validate_version_config.__name__,
]
