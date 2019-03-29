<?php
/**
 * available variables is
 *  $rsvp: the state of the rsvp, if empty this is undesided.
 *  $rsvp_status: the state of the rsvp, contains descriptive text
 *  $name: name of the volunteer
 *  $phone: the volunteers phone number
 *  $email: the volunteers email address
 *  $form: the rsvp form
 *  $is_internet_explorer: true/false - browser is IE11 and below.
 */
?>

<?php if ($is_internet_explorer && !isset($_GET['is_opened_in_blank'])): ?>
  <!--
    In IE11 and below we are experiencing issues when a user clicks on a link to this
    page from within their own PDF inline viewer. Apparently this disables some JS which
    breaks the form functionality.

    SO... when IE is detected we show a modal with a link to open the page in an external
    tab.
   -->
  <div class="blocker">
    <div class="blocker__modal">
      <h1><?php print t('Tak!'); ?></h1>

      <div>
        <p><strong><?php print t('På denne side kan du bekræfte invitationen til valget.'); ?></strong></p>
        <p><?php print t('Klik på nedenstående knap for at for at fortsætte.'); ?></p>
      </div>

      <br>

      <div class="text-center">
        <a href="<?php print $_SERVER['REQUEST_URI']; ?>?is_opened_in_blank=true" class="btn btn-lg btn-secondary" target="_new">
          <?php print t('Fortsæt'); ?>
          <span class="icon fa fa-arrow-right"></span>
        </a>
      </div>
    </div>
  </div>
<?php endif; ?>

<?php if (isset($name)): ?>
  <h2><?php print t('Hej %name', array('%name' => $name)) ?></h2>
<?php endif; ?>
<div>
  <p><?php print t('Her kan du tilkendegive om du ønsker at udfylde den post vi har tiltænkt dig i det kommende valg.') ?></p>
</div>
<?php if($rsvp): ?>
<p> <?php print t('Vi har registreret følgende svar: '); ?> <strong> <?php print $rsvp_status; ?></strong>.</p>
<?php endif; ?>
<br />
<table>
  <tr>
    <td class="col-sm-3 col-md-3">
      <strong><?php print t('Funktion:'); ?></strong><br />
    </td>
    <td class="col-sm-9 col-md-9">
      <?php if (!empty($params['!position_description'])) : ?>
        <?php print $params['!position_description']; ?>
      <?php else: ?>
        <?php print $params['!position']; ?>
      <?php endif; ?>
    </td>
  </tr>
  <tr>
    <td class="col-sm-3 col-md-3">
      <strong>Dato:</strong><br />
    </td>
    <td class="col-sm-9 col-md-9">
      <?php print $params['!election_date']; ?><br />
    </td>
  </tr>

  <tr>
    <td class="col-sm-3 col-md-3">
      <strong><?php print t('Tidspunkter:'); ?></strong><br />
    </td>
    <td class="col-sm-9 col-md-9">
      <?php print $params['!time']; ?><br />
    </td>
  </tr>
  <tr>
    <br />
  </tr>
  <tr>
    <td class="col-sm-3 col-md-3">
      <strong><?php print t('Valgsted:'); ?></strong>
    </td>
    <td class="col-sm-9 col-md-9">
      <?php print $params['!polling_station']; ?><br />
    </td>
  </tr>
  <tr>
    <td class="col-sm-3 col-md-3">
    </td>
    <td class="col-sm-9 col-md-9">
      <?php print nl2br($params['!polling_station_address']); ?><br />
    </td>
  </tr>
</table>
<?php print $post_script ?>
