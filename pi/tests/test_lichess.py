import unittest
from unittest import mock

import requests

from chessds_bridge import lichess
from chessds_bridge.lichess import Lichess, LichessError


class FakeResponse:
    status_code = 200

    def close(self):
        pass

    def json(self):
        return {}


class LichessClientTest(unittest.TestCase):
    def setUp(self):
        self.li = Lichess("token")
        self.li.session.request = mock.Mock(return_value=FakeResponse())
        self.li.session.close = mock.Mock()

    def test_idle_connections_are_dropped_before_a_request(self):
        self.li.last_request -= lichess.IDLE_RESET + 1
        self.li.move("game0001", "e2e4")
        self.li.move("game0001", "e7e5")
        self.li.session.close.assert_called_once()
        self.assertEqual(self.li.session.request.call_count, 2)

    def test_busy_connections_are_kept(self):
        self.li.move("game0001", "e2e4")
        self.li.session.close.assert_not_called()

    def test_streams_use_their_own_connections(self):
        self.li.stream_session.request = mock.Mock(return_value=FakeResponse())
        self.li.stream_events()
        self.li.session.request.assert_not_called()
        self.li.stream_session.request.assert_called_once()

    def test_only_failed_connects_are_retried(self):
        retry = self.li.session.get_adapter("https://lichess.org").max_retries
        self.assertEqual((retry.connect, retry.read, retry.status), (2, 0, 0))

    def test_network_errors_become_lichess_errors(self):
        self.li.session.request.side_effect = requests.ConnectionError("reset by peer")
        with self.assertRaises(LichessError) as raised:
            self.li.resign("game0001")
        self.assertEqual(raised.exception.status, 0)
        self.assertEqual(str(raised.exception), "Network error (ConnectionError)")


if __name__ == "__main__":
    unittest.main()
