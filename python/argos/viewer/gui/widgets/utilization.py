def FormatUtilizPct(utiliz_pct):
    percentage = utiliz_pct * 100
    if percentage < 10:
        return f'{percentage:.1f}'.rstrip('0').rstrip('.')
    return f'{percentage:.0f}'