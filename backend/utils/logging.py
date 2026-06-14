import sys

from loguru import logger

from backend.config.settings import settings


def configure_logging() -> None:
    logger.remove()

    if settings.debug:
        logger.add(
            sys.stderr,
            level="DEBUG",
            colorize=True,
            format=(
                "<green>{time:HH:mm:ss}</green> | "
                "<level>{level: <8}</level> | "
                "<cyan>{name}</cyan>:<cyan>{line}</cyan> - "
                "<level>{message}</level>"
            ),
        )
    else:
        logger.add(sys.stderr, level="INFO", colorize=False)
        logger.add(
            "/tmp/ark.log",
            level="INFO",
            rotation="50 MB",
            retention="7 days",
            compression="gz",
        )
