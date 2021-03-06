# Copyright (c) 2009, 2010, 2011 Google Inc. All rights reserved.
# Copyright (c) 2009 Apple Inc. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import os
import re
import shutil
import sys

from webkitpy.common.memoized import memoized
from webkitpy.common.system.deprecated_logging import log
from webkitpy.common.system.executive import Executive, run_command, ScriptError
from webkitpy.common.system import ospath

from .scm import AuthenticationError, SCM, commit_error_handler


# A mixin class that represents common functionality for SVN and Git-SVN.
class SVNRepository:
    def has_authorization_for_realm(self, realm, home_directory=os.getenv("HOME")):
        # Assumes find and grep are installed.
        if not os.path.isdir(os.path.join(home_directory, ".subversion")):
            return False
        find_args = ["find", ".subversion", "-type", "f", "-exec", "grep", "-q", realm, "{}", ";", "-print"]
        find_output = self.run(find_args, cwd=home_directory, error_handler=Executive.ignore_error).rstrip()
        if not find_output or not os.path.isfile(os.path.join(home_directory, find_output)):
            return False
        # Subversion either stores the password in the credential file, indicated by the presence of the key "password",
        # or uses the system password store (e.g. Keychain on Mac OS X) as indicated by the presence of the key "passtype".
        # We assume that these keys will not coincide with the actual credential data (e.g. that a person's username
        # isn't "password") so that we can use grep.
        if self.run(["grep", "password", find_output], cwd=home_directory, return_exit_code=True) == 0:
            return True
        return self.run(["grep", "passtype", find_output], cwd=home_directory, return_exit_code=True) == 0


class SVN(SCM, SVNRepository):
    # FIXME: These belong in common.config.urls
    svn_server_host = "svn.webkit.org"
    svn_server_realm = "<http://svn.webkit.org:80> Mac OS Forge"

    def __init__(self, cwd, patch_directories, executive=None):
        SCM.__init__(self, cwd, executive)
        self._bogus_dir = None
        if patch_directories == []:
            # FIXME: ScriptError is for Executive, this should probably be a normal Exception.
            raise ScriptError(script_args=svn_info_args, message='Empty list of patch directories passed to SCM.__init__')
        elif patch_directories == None:
            self._patch_directories = [ospath.relpath(cwd, self.checkout_root)]
        else:
            self._patch_directories = patch_directories

    @staticmethod
    def in_working_directory(path):
        return os.path.isdir(os.path.join(path, '.svn'))

    @classmethod
    def find_uuid(cls, path):
        if not cls.in_working_directory(path):
            return None
        return cls.value_from_svn_info(path, 'Repository UUID')

    @classmethod
    def value_from_svn_info(cls, path, field_name):
        svn_info_args = ['svn', 'info', path]
        info_output = run_command(svn_info_args).rstrip()
        match = re.search("^%s: (?P<value>.+)$" % field_name, info_output, re.MULTILINE)
        if not match:
            raise ScriptError(script_args=svn_info_args, message='svn info did not contain a %s.' % field_name)
        return match.group('value')

    @staticmethod
    def find_checkout_root(path):
        uuid = SVN.find_uuid(path)
        # If |path| is not in a working directory, we're supposed to return |path|.
        if not uuid:
            return path
        # Search up the directory hierarchy until we find a different UUID.
        last_path = None
        while True:
            if uuid != SVN.find_uuid(path):
                return last_path
            last_path = path
            (path, last_component) = os.path.split(path)
            if last_path == path:
                return None

    @staticmethod
    def commit_success_regexp():
        return "^Committed revision (?P<svn_revision>\d+)\.$"

    @memoized
    def svn_version(self):
        return self.run(['svn', '--version', '--quiet'])

    def working_directory_is_clean(self):
        return self.run(["svn", "diff"], cwd=self.checkout_root, decode_output=False) == ""

    def clean_working_directory(self):
        # Make sure there are no locks lying around from a previously aborted svn invocation.
        # This is slightly dangerous, as it's possible the user is running another svn process
        # on this checkout at the same time.  However, it's much more likely that we're running
        # under windows and svn just sucks (or the user interrupted svn and it failed to clean up).
        self.run(["svn", "cleanup"], cwd=self.checkout_root)

        # svn revert -R is not as awesome as git reset --hard.
        # It will leave added files around, causing later svn update
        # calls to fail on the bots.  We make this mirror git reset --hard
        # by deleting any added files as well.
        added_files = reversed(sorted(self.added_files()))
        # added_files() returns directories for SVN, we walk the files in reverse path
        # length order so that we remove files before we try to remove the directories.
        self.run(["svn", "revert", "-R", "."], cwd=self.checkout_root)
        for path in added_files:
            # This is robust against cwd != self.checkout_root
            absolute_path = self.absolute_path(path)
            # Completely lame that there is no easy way to remove both types with one call.
            if os.path.isdir(path):
                os.rmdir(absolute_path)
            else:
                os.remove(absolute_path)

    def status_command(self):
        return ['svn', 'status']

    def _status_regexp(self, expected_types):
        field_count = 6 if self.svn_version() > "1.6" else 5
        return "^(?P<status>[%s]).{%s} (?P<filename>.+)$" % (expected_types, field_count)

    def _add_parent_directories(self, path):
        """Does 'svn add' to the path and its parents."""
        if self.in_working_directory(path):
            return
        dirname = os.path.dirname(path)
        # We have dirname directry - ensure it added.
        if dirname != path:
            self._add_parent_directories(dirname)
        self.add(path)

    def add(self, path, return_exit_code=False):
        self._add_parent_directories(os.path.dirname(os.path.abspath(path)))
        return self.run(["svn", "add", path], return_exit_code=return_exit_code)

    def delete(self, path):
        parent, base = os.path.split(os.path.abspath(path))
        return self.run(["svn", "delete", "--force", base], cwd=parent)

    def exists(self, path):
        return not self.run(["svn", "info", path], return_exit_code=True, decode_output=False)

    def changed_files(self, git_commit=None):
        status_command = ["svn", "status"]
        status_command.extend(self._patch_directories)
        # ACDMR: Addded, Conflicted, Deleted, Modified or Replaced
        return self.run_status_and_extract_filenames(status_command, self._status_regexp("ACDMR"))

    def changed_files_for_revision(self, revision):
        # As far as I can tell svn diff --summarize output looks just like svn status output.
        # No file contents printed, thus utf-8 auto-decoding in self.run is fine.
        status_command = ["svn", "diff", "--summarize", "-c", revision]
        return self.run_status_and_extract_filenames(status_command, self._status_regexp("ACDMR"))

    def revisions_changing_file(self, path, limit=5):
        revisions = []
        # svn log will exit(1) (and thus self.run will raise) if the path does not exist.
        log_command = ['svn', 'log', '--quiet', '--limit=%s' % limit, path]
        for line in self.run(log_command, cwd=self.checkout_root).splitlines():
            match = re.search('^r(?P<revision>\d+) ', line)
            if not match:
                continue
            revisions.append(int(match.group('revision')))
        return revisions

    def conflicted_files(self):
        return self.run_status_and_extract_filenames(self.status_command(), self._status_regexp("C"))

    def added_files(self):
        return self.run_status_and_extract_filenames(self.status_command(), self._status_regexp("A"))

    def deleted_files(self):
        return self.run_status_and_extract_filenames(self.status_command(), self._status_regexp("D"))

    @staticmethod
    def supports_local_commits():
        return False

    def display_name(self):
        return "svn"

    def head_svn_revision(self):
        return self.value_from_svn_info(self.checkout_root, 'Revision')

    # FIXME: This method should be on Checkout.
    def create_patch(self, git_commit=None, changed_files=None):
        """Returns a byte array (str()) representing the patch file.
        Patch files are effectively binary since they may contain
        files of multiple different encodings."""
        if changed_files == []:
            return ""
        elif changed_files == None:
            changed_files = []
        return self.run([self.script_path("svn-create-patch")] + changed_files,
            cwd=self.checkout_root, return_stderr=False,
            decode_output=False)

    def committer_email_for_revision(self, revision):
        return self.run(["svn", "propget", "svn:author", "--revprop", "-r", revision]).rstrip()

    def contents_at_revision(self, path, revision):
        """Returns a byte array (str()) containing the contents
        of path @ revision in the repository."""
        remote_path = "%s/%s" % (self._repository_url(), path)
        return self.run(["svn", "cat", "-r", revision, remote_path], decode_output=False)

    def diff_for_revision(self, revision):
        # FIXME: This should probably use cwd=self.checkout_root
        return self.run(['svn', 'diff', '-c', revision])

    def _bogus_dir_name(self):
        if sys.platform.startswith("win"):
            parent_dir = tempfile.gettempdir()
        else:
            parent_dir = sys.path[0]  # tempdir is not secure.
        return os.path.join(parent_dir, "temp_svn_config")

    def _setup_bogus_dir(self, log):
        self._bogus_dir = self._bogus_dir_name()
        if not os.path.exists(self._bogus_dir):
            os.mkdir(self._bogus_dir)
            self._delete_bogus_dir = True
        else:
            self._delete_bogus_dir = False
        if log:
            log.debug('  Html: temp config dir: "%s".', self._bogus_dir)

    def _teardown_bogus_dir(self, log):
        if self._delete_bogus_dir:
            shutil.rmtree(self._bogus_dir, True)
            if log:
                log.debug('  Html: removed temp config dir: "%s".', self._bogus_dir)
        self._bogus_dir = None

    def diff_for_file(self, path, log=None):
        self._setup_bogus_dir(log)
        try:
            args = ['svn', 'diff']
            if self._bogus_dir:
                args += ['--config-dir', self._bogus_dir]
            args.append(path)
            return self.run(args, cwd=self.checkout_root)
        finally:
            self._teardown_bogus_dir(log)

    def show_head(self, path):
        return self.run(['svn', 'cat', '-r', 'BASE', path], decode_output=False)

    def _repository_url(self):
        return self.value_from_svn_info(self.checkout_root, 'URL')

    def apply_reverse_diff(self, revision):
        # '-c -revision' applies the inverse diff of 'revision'
        svn_merge_args = ['svn', 'merge', '--non-interactive', '-c', '-%s' % revision, self._repository_url()]
        log("WARNING: svn merge has been known to take more than 10 minutes to complete.  It is recommended you use git for rollouts.")
        log("Running '%s'" % " ".join(svn_merge_args))
        # FIXME: Should this use cwd=self.checkout_root?
        self.run(svn_merge_args)

    def revert_files(self, file_paths):
        # FIXME: This should probably use cwd=self.checkout_root.
        self.run(['svn', 'revert'] + file_paths)

    def commit_with_message(self, message, username=None, password=None, git_commit=None, force_squash=False, changed_files=None):
        # git-commit and force are not used by SVN.
        svn_commit_args = ["svn", "commit"]

        if not username and not self.has_authorization_for_realm(self.svn_server_realm):
            raise AuthenticationError(self.svn_server_host)
        if username:
            svn_commit_args.extend(["--username", username])

        svn_commit_args.extend(["-m", message])

        if changed_files:
            svn_commit_args.extend(changed_files)

        if self.dryrun:
            _log = logging.getLogger("webkitpy.common.system")
            _log.debug('Would run SVN command: "' + " ".join(svn_commit_args) + '"')

            # Return a string which looks like a commit so that things which parse this output will succeed.
            return "Dry run, no commit.\nCommitted revision 0."

        return self.run(svn_commit_args, cwd=self.checkout_root, error_handler=commit_error_handler)

    def svn_commit_log(self, svn_revision):
        svn_revision = self.strip_r_from_svn_revision(svn_revision)
        return self.run(['svn', 'log', '--non-interactive', '--revision', svn_revision])

    def last_svn_commit_log(self):
        # BASE is the checkout revision, HEAD is the remote repository revision
        # http://svnbook.red-bean.com/en/1.0/ch03s03.html
        return self.svn_commit_log('BASE')

    def propset(self, pname, pvalue, path):
        dir, base = os.path.split(path)
        return self.run(['svn', 'pset', pname, pvalue, base], cwd=dir)

    def propget(self, pname, path):
        dir, base = os.path.split(path)
        return self.run(['svn', 'pget', pname, base], cwd=dir).encode('utf-8').rstrip("\n")
