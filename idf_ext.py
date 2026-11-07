import os
import subprocess
import sys

# Действия idf.py, перед которыми нужно поднять номер сборки.
# Важно: bump выполняется ДО запуска ninja, иначе часть .cpp успевает
# скомпилироваться со старым version.h и в прошивке оказываются разные версии.
BUMP_ACTIONS = {
    'all',              # idf.py build
    'flash',            # idf.py flash
    'app',              # idf.py app
    'size',
    'size-components',
    'size-files',
}


def action_extensions(base_actions, project_path):
    def bump_build(ctx, global_args, tasks):
        if not any(task.name in BUMP_ACTIONS for task in tasks):
            return

        script = os.path.join(project_path, 'components', 'device_manager', 'bump_version.py')
        header = os.path.join(project_path, 'components', 'device_manager', 'include', 'version.h')

        try:
            subprocess.check_call([sys.executable, script, header])
        except (OSError, subprocess.CalledProcessError) as e:
            raise SystemExit('bump_version failed: {}'.format(e))

    return {
        'global_action_callbacks': [bump_build],
    }
