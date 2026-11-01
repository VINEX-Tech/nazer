def test_admin_dashboard_requires_auth(client):
    resp = client.get("/v1/admin/dashboard")
    assert resp.status_code == 401


def test_admin_dashboard_rejects_wrong_credentials(client):
    resp = client.get("/v1/admin/dashboard", auth=("wrong", "creds"))
    assert resp.status_code == 401


def test_admin_dashboard_accepts_correct_credentials(client):
    resp = client.get("/v1/admin/dashboard", auth=("testadmin", "testpass123"))
    assert resp.status_code == 200
    assert "total_devices" in resp.json()
