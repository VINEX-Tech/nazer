# services/violation_service.py: Business logic for violations and driver scoring

def calculate_fine(speed_kmh: float, speed_limit_kmh: float) -> float:
    """
    Returns the fine amount in EGP based on excess speed.
    Tiers match the NAZER spec exactly.
    """
    excess = speed_kmh - speed_limit_kmh
    if excess <= 10:
        return 50.0
    elif excess <= 20:
        return 100.0
    elif excess <= 40:
        return 200.0
    else:
        return 500.0


def calculate_driver_score(violations: list) -> int:
    """
    Compute a driver score 0–100.
    Starts at 100, deducts points per violation based on severity.
    Minimum score is 0.
    """
    score = 100
    for v in violations:
        excess = (v.excess_kmh or 0)
        if excess <= 10:
            score -= 2
        elif excess <= 20:
            score -= 5
        elif excess <= 40:
            score -= 10
        else:
            score -= 20
    return max(0, score)
