import asyncio

from backend.memory.session_manager import SessionManager


async def test_add_and_retrieve_message() -> None:
    sm = SessionManager()
    await sm.add_message("sess1", "user", "Hello")
    history = await sm.get_history("sess1")
    assert len(history) == 1
    assert history[0].role == "user"
    assert history[0].content == "Hello"


async def test_multi_turn_history_preserved() -> None:
    sm = SessionManager()
    await sm.add_message("sess2", "user", "Hi")
    await sm.add_message("sess2", "assistant", "Hello!")
    await sm.add_message("sess2", "user", "How are you?")
    history = await sm.get_history("sess2")
    assert len(history) == 3
    assert history[0].role == "user"
    assert history[1].role == "assistant"


async def test_history_trimmed_to_max() -> None:
    sm = SessionManager()
    sm._max_history = 4
    for i in range(10):
        role = "user" if i % 2 == 0 else "assistant"
        await sm.add_message("sess3", role, f"msg {i}")
    history = await sm.get_history("sess3")
    assert len(history) <= 4


async def test_clear_session() -> None:
    sm = SessionManager()
    await sm.add_message("sess4", "user", "test")
    await sm.clear_session("sess4")
    assert not sm.session_exists("sess4")
    history = await sm.get_history("sess4")
    assert history == []


async def test_session_exists() -> None:
    sm = SessionManager()
    assert not sm.session_exists("nonexistent")
    await sm.add_message("sess5", "user", "hi")
    assert sm.session_exists("sess5")


async def test_get_active_count() -> None:
    sm = SessionManager()
    await sm.add_message("a", "user", "hi")
    await sm.add_message("b", "user", "hello")
    count = await sm.get_active_count()
    assert count == 2


async def test_concurrent_access_is_safe() -> None:
    sm = SessionManager()

    async def add_messages(session_id: str, count: int) -> None:
        for i in range(count):
            await sm.add_message(session_id, "user", f"msg {i}")

    await asyncio.gather(
        add_messages("concurrent", 20),
        add_messages("concurrent", 20),
    )
    history = await sm.get_history("concurrent")
    assert len(history) <= sm._max_history
