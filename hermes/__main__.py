from __future__ import annotations

from .config import load_app_config, load_mailbox_profiles
from .service import HermesService, ensure_runtime_dirs, setup_logging


def main() -> None:
	setup_logging()
	app = load_app_config()
	profiles = load_mailbox_profiles(app)
	ensure_runtime_dirs(app, profiles)
	service = HermesService(app, profiles)
	service.serve_forever()


if __name__ == "__main__":
	main()
