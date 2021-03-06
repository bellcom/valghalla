<?php

/**
 * @file
 * Valghalla_internal_server.batch.inc.
 */

use ValghallaInternalServer\ValghallaLogger;
use ValghallaInternalServer\ExternalWebservice;

/**
 * Node clone batch definition.
 *
 * @param mixed $uuids
 *   Array of uuid.
 *
 * @return array
 *   Batch definition.
 */
function valghalla_internal_server_batch_clone($uuids) {
  ValghallaLogger::info('Initializing clone operation');

  ValghallaLogger::info(sprintf('Preparing to clone %d node(s)', count($uuids)));
  drupal_set_message(t('Preparing to clone @num node(s).', array('@num' => count($uuids))));

  $operations = array();
  for ($i = 0; $i < count($uuids); $i++) {
    $operations[] = array(
      'valghalla_internal_server_batch_clone_op',
      array(
        $uuids[$i],
        t('(Cloning operation @operation)', array('@operation' => $i)),
      ),
    );
  }
  $batch = array(
    'operations' => $operations,
    'title' => t('Processing cloning'),
    'init_message' => t('Cloning is starting.'),
    'progress_message' => t('Processed @current out of @total.'),
    'error_message' => t('Cloning has encountered an error.'),
    'finished' => 'valghalla_internal_server_batch_clone_finished',
  );

  return $batch;
}

/**
 * Node clone batch operation.
 *
 * Clones the node locally.
 *
 * @param mixed $uuid
 *   Array of uuids.
 * @param mixed $operation_details
 *   Details of operation.
 * @param mixed $context
 *   Array for context.
 */
function valghalla_internal_server_batch_clone_op($uuid, $operation_details, &$context) {
  // Load node local data we are about to clone.
  $existingNode = NULL;
  $entities = entity_uuid_load('node', array($uuid));
  if (!empty($entities) && is_array($entities)) {
    $existingNode = reset($entities);
  }

  // Report progress.
  if ($existingNode) {
    ValghallaLogger::info(sprintf('Cloning node %s', $existingNode->title));
    $context['message'] = t('Cloning node %title', array('%title' => $existingNode->title));
  }
  else {
    ValghallaLogger::info('Cloning new node');
    $context['message'] = t('Cloning new node', array());
  }

  // Actual webservice call.
  $ws = new ExternalWebservice();
  $data = $ws->exportByUuid($uuid);

  // We got the node export data.
  if (strpos($data, 'node_export_serialize') === 0) {
    // Prepare the node, but don't save it right away.
    $import = node_export_import($data);

    if ($import['success']) {
      // We expect only one node to be imported.
      $nid = reset($import['nids']);
      $node = node_load($nid);

      // Delete the item from queue on the external server.
      $ws->removeFromQueueByUuid($uuid);

      // Check if the same node has been modified locally.
      // if yes, we have a conflict.
      if ($existingNode && $existingNode->changed > variable_get('valgalla_internal_server_last_clone_timestamp')) {

        // We have a conflict, the local node was modified after the last pull.
        // Return to the previous revision, as internal server always wins.
        $logMessage = sprintf('Existing Node (revision %d) conflicts with the pulled node (revision %d). Reverting to previous revision %d', $existingNode->vid, $node->vid, $existingNode->vid);

        $existingNodeRestored = entity_revision_load('node', $existingNode->vid);
        $existingNodeRestored->revision = TRUE;
        $existingNodeRestored->log = $logMessage;
        entity_save('node', $existingNodeRestored);

        $logMessage = sprintf('Existing Node "%s" (revision %d) conflicts with the pulled node (revision %d). Reverting to previous revision %d', $existingNode->title, $existingNode->vid, $node->vid, $existingNode->vid);
        ValghallaLogger::warning($logMessage);
      }
      else {
        // Get all new and updated seats.
        $seats = valghalla_internal_server_get_updated_seats($node, $existingNode);

        // Count seats validation.
        $seatValidationSuccessful = 0;
        $seatValidationFailed = 0;

        foreach ($seats as $seat) {
          $seatValidationStatus = valghalla_internal_server_validate_volunteer_seat($node, $seat);

          if ($seatValidationStatus['valid']) {
            // Notify volunteer.
            valghalla_volunteers_notify_volunteer($seat);

            $seatValidationSuccessful++;
          }
          else {
            ValghallaLogger::warning(sprintf('Node "%s" failed validation, messages %s', $node->title, implode('; ', $seatValidationStatus['errors'])));

            /** @var EntityInterface $valghalla_volunteer_validation_record */
            $valghalla_volunteer_validation_record = entity_get_controller('valghalla_volunteer_validation_record')->create(array(
              'type' => 'external_server_sync',
              'vol_id' => $node->nid,
              'election_id' => $seatValidationStatus['election_nid'],
              'message' => implode("\n", $seatValidationStatus['errors']),
            ));
            $valghalla_volunteer_validation_record->save();

            // Freeing the seat.
            valghalla_internal_server_volunteer_free_seat($seat);

            $seatValidationFailed++;
          }
        }

        ValghallaLogger::info(sprintf('Node "%s" imported into %s. %d/%d seats were successfully validated. %d seats failed validation', $node->title, url('node/' . $nid), $seatValidationSuccessful, count($seats), $seatValidationFailed));
        $context['results'][] = array(
          'message' => t('Node "@title" imported into <a href="@link">@title</a>. @valid/@total seats were successfully validated. @failed seats failed validation', array(
            '@title' => $node->title,
            '@link' => url('node/' . $nid),
            '@valid' => $seatValidationSuccessful,
            '@total' => count($seats),
            '@failed' => $seatValidationFailed,
          )),
          'type' => 'status',
        );

        // If we have any failed seats, push the node to free the seats on
        // external server.
        if ($seatValidationFailed) {
          // Exporting with serialize formatter, and pushing the node.
          $export = node_export(intval($node->nid), 'serialize', 't', TRUE);
          $data = $export['output'];
          $result = $ws->pushNodeSerialized($data);

          // Push was unsuccessful.
          if (!$result->nid) {
            if (is_array($result)) {
              $result = implode(' ', $result);
            }

            ValghallaLogger::error(sprintf('Could not free seat(s) for node "%s", error: %s', $node->title, $result));
          }
          else {
            ValghallaLogger::info(sprintf('Seat(s) for node "%s" has been freed', $node->title));

            // Delete the item from queue.
            valghalla_synch_queue_remove_item($node->uuid);
          }
        }
        else {
          // Else just delete the item from queue.
          valghalla_synch_queue_remove_item($node->uuid);
        }
      }
    }
  }
  else {
    // Report the problem.
    if ($existingNode) {
      ValghallaLogger::error(sprintf('Node "%s" import failed, error: %s', $existingNode->title, $data));
      $context['results'][] = array(
        'message' => t('Node "@title" import failed. Message: @message', array(
          '@title' => $existingNode->title,
          '@message' => $data,
        )),
        'type' => 'warning',
      );
    }
    else {
      ValghallaLogger::error(sprintf('Node "%s" import failed, error: %s', $uuid, $data));
      $context['results'][] = array(
        'message' => t('Node "@uuid" import failed. Message: @message', array(
          '@uuid' => $uuid,
          '@message' => $data,
        )),
        'type' => 'warning',
      );
    }
  }
}

/**
 * Batch finished callback for clone operation.
 *
 * @param bool $success
 *   Status.
 * @param mixed $results
 *   Array of result messages.
 * @param mixed $operations
 *   Array of operation messages.
 */
function valghalla_internal_server_batch_clone_finished($success, $results, $operations) {
  if ($success) {
    // Counting successful.
    $successCount = 0;
    foreach ($results as $result) {
      if ($result['type'] === 'status') {
        $successCount++;
      }
    }

    ValghallaLogger::info(sprintf('Finishing clone operation, %d out of %d node(s) have been cloned', $successCount, count($results)));
    drupal_set_message(t('@successCount out of @totalCount node(s) have been cloned.', array(
      '@successCount' => $successCount,
      '@totalCount' => count($results),
    )));

    foreach ($results as $result) {
      drupal_set_message($result['message'], $result['type']);
    }
  }
  else {
    // An error occurred.
    // $operations contains the operations that remained unprocessed.
    $error_operation = reset($operations);

    ValghallaLogger::error(sprintf('Error occurred during clone operation. Error: %s', print_r($error_operation[0], TRUE)));
    drupal_set_message(
      t('An error occurred while processing {
      @operation
} with arguments : @args',
        array(
          '@operation' => $error_operation[0],
          '@args' => print_r($error_operation[0], TRUE),
        )
      ),
      'error'
    );
  }

  // Updating last pull timestamp.
  variable_set('valgalla_internal_server_last_clone_timestamp', time());
}

/**
 * Resource push batch definition.
 *
 * @param mixed $items
 *   Array of items.
 *
 * @return mixed
 *   Batch definition.
 */
function valghalla_internal_server_batch_push($items) {
  ValghallaLogger::info('Initializing push operation');

  ValghallaLogger::info(sprintf('Preparing to push %d resource(s)', count($items)));
  drupal_set_message(t('Preparing to push @num resource(s).', array('@num' => count($items))));

  $operations = array();
  for ($i = 0; $i < count($items); $i++) {
    $operations[] = array(
      'valghalla_internal_server_batch_push_op',
      array(
        $items[$i],
        t('(Push operation @operation)', array('@operation' => $i)),
      ),
    );
  }
  $batch = array(
    'operations' => $operations,
    'title' => t('Processing push'),
    'init_message' => t('Push is starting.'),
    'progress_message' => t('Processed @current out of @total.'),
    'error_message' => t('Push has encountered an error.'),
    'finished' => 'valghalla_internal_server_batch_push_finished',
  );

  return $batch;
}

/**
 * Resource push batch operation.
 *
 * Pushed the resource to the external server,
 * upon success removes the item from a synch queue.
 *
 * @param mixed $item
 *   Item to be pushed.
 * @param mixed $operation_details
 *   Details of operation.
 * @param mixed $context
 *   Array for context.
 */
function valghalla_internal_server_batch_push_op($item, $operation_details, &$context) {
  $ws = new ExternalWebservice();

  // Deleting the item.
  if ($item->deleted) {
    if ($item->type == 'taxonomy_term') {
      // Report progress.
      ValghallaLogger::info(sprintf('Deleting taxonomy term %s', $item->target_uuid));
      $context['message'] = t('Deleting taxonomy term @uuid', array('@uuid' => $item->target_uuid));

      // Check if term exists on external server.
      $item_on_external_server = $ws->getTermByUuid($item->target_uuid);

      if (!empty($item_on_external_server)) {
        // Deleting item on remote.
        $result = $ws->deleteTerm($item->target_uuid);

        if ($result === 3) {
          ValghallaLogger::info(sprintf('Term %s is successfully deleted on external server', $item->target_uuid));
          $context['results'][] = array(
            'message' => t('Term "@uuid" is successfully deleted on external server', array(
              '@uuid' => $item->target_uuid,
            )),
            'type' => 'status',
          );

          valghalla_synch_queue_remove_item($item->target_uuid);
        }
      }
      else {
        // Item is not present on remote, delete it from the queue.
        valghalla_synch_queue_remove_item($item->target_uuid);

        ValghallaLogger::info(sprintf('Term %s is not preset on external server, deleted the item from the queue', $item->target_uuid));
        $context['results'][] = array(
          'message' => t('Term "@uuid" not present on external server, item removed from the queue', array(
            '@uuid' => $item->target_uuid,
          )),
          'type' => 'status',
        );
      }
    }
    elseif ($item->type == 'node') {
      // Report progress.
      ValghallaLogger::info(sprintf('Deleting node %s', $item->target_uuid));
      $context['message'] = t('Deleting node @uuid', array('@uuid' => $item->target_uuid));

      // Check if term exists on external server.
      $item_on_external_server = $ws->getNodeByUuid($item->target_uuid);

      if (!empty($item_on_external_server)) {
        // Deleting item on external server.
        $result = $ws->deleteNode($item->target_uuid);
        if ($result === TRUE) {
          ValghallaLogger::info(sprintf('Node %s is successfully deleted on external server', $item->target_uuid));
          $context['results'][] = array(
            'message' => t('Node "@uuid" is successfully deleted on external server', array(
              '@uuid' => $item->target_uuid,
            )),
            'type' => 'status',
          );

          valghalla_synch_queue_remove_item($item->target_uuid);
        }
      }
      else {
        // Item is not present on remote, delete it from the queue.
        valghalla_synch_queue_remove_item($item->target_uuid);

        ValghallaLogger::info(sprintf('Node %s is not preset on external server, deleted the item from the queue', $item->target_uuid));
        $context['results'][] = array(
          'message' => t('Node "@uuid" not present on external server, item removed from the queue', array(
            '@uuid' => $item->target_uuid,
          )),
          'type' => 'status',
        );
      }
    }
  }
  // Pushing the created/updated item.
  else {
    $existingItem = NULL;
    $entities = entity_uuid_load($item->type, array($item->target_uuid));
    if (!empty($entities) && is_array($entities)) {
      $existingItem = reset($entities);
    }

    if ($existingItem) {
      if ($item->type == 'taxonomy_term') {
        // Report progress.
        ValghallaLogger::info(sprintf('Pushing taxonomy term %s', $existingItem->name));
        $context['message'] = t('Pushing taxonomy term @title', array('@title' => $existingItem->name));

        // Actual webservice call.
        $result = $ws->pushTerm($existingItem);
        if ($result === 1) {
          ValghallaLogger::info(sprintf('Term %s is successfully pushed and created on external server', $existingItem->name));
          $context['results'][] = array(
            'message' => t('Term "@title" is successfully pushed and created on external server', array(
              '@title' => $existingItem->name,
            )),
            'type' => 'status',
          );

          valghalla_synch_queue_remove_item($existingItem->uuid);
        }
        elseif ($result === 2) {
          ValghallaLogger::info(sprintf('Term %s is successfully pushed and updated on external server', $existingItem->name));
          $context['results'][] = array(
            'message' => t('Term "@title" is successfully pushed and updated on external server', array(
              '@title' => $existingItem->name,
            )),
            'type' => 'status',
          );

          valghalla_synch_queue_remove_item($existingItem->uuid);
        }
        else {
          if (is_array($result)) {
            $result = implode(' ', $result);
          }

          ValghallaLogger::error(sprintf('Term %s push failed, error: %s', $existingItem->name, $result));
          $context['results'][] = array(
            'message' => t('Term "@title" push failed, message: @message', array(
              '@title' => $existingItem->name,
              '@message' => $result,
            )),
            'type' => 'warning',
          );
        }
      }
      elseif ($item->type == 'node') {
        // Report progress.
        ValghallaLogger::info(sprintf('Pushing node %s', $existingItem->title));
        $context['message'] = t('Pushing node @title', array('@title' => $existingItem->title));

        // Exporting with serialize formatter.
        $export = node_export(intval($existingItem->nid), 'serialize', 't', TRUE);

        $op_success = FALSE;

        // Checking if export was successful.
        if ($export['success']) {
          // Actual webservice call.
          $result = $ws->pushNodeSerialized($export['output']);

          // Operation was successful.
          if (isset($result->nid)) {
            ValghallaLogger::info(sprintf('Node %s is successfully pushed, and its remote nid is %d', $existingItem->title, $result->nid));
            $context['results'][] = array(
              'message' => t('Node "@title" is successfully pushed, and its remote nid is @rnid', array(
                '@title' => $existingItem->title,
                '@rnid' => $result->nid,
              )),
              'type' => 'status',
            );

            valghalla_synch_queue_remove_item($existingItem->uuid);
            $op_success = TRUE;
          }
        }
        else {
          $result = $export['output'];
        }

        // Unsuccessful outcome, either export or push.
        if (!$op_success) {
          if (is_array($result)) {
            $result = implode(' ', $result);
          }

          ValghallaLogger::error(sprintf('Node %s push failed, error: %s', $existingItem->title, $result));
          $context['results'][] = array(
            'message' => t('Node "@title" push failed, message: @message', array(
              '@title' => $existingItem->title,
              '@message' => $result,
            )),
            'type' => 'warning',
          );
        }
      }
    }
  }
}

/**
 * Batch finished callback for push operation.
 *
 * @param bool $success
 *   Status.
 * @param mixed $results
 *   Array of result messages.
 * @param mixed $operations
 *   Array of operation messages.
 */
function valghalla_internal_server_batch_push_finished($success, $results, $operations) {
  if ($success) {
    // Counting successful.
    $successCount = 0;
    foreach ($results as $result) {
      if ($result['type'] === 'status') {
        $successCount++;
      }
    }

    ValghallaLogger::info(sprintf('Finishing push operation, %d out of %d resource(s) have been pushed', $successCount, count($results)));
    drupal_set_message(t('@successCount out of @totalCount resource(s) have been committed.', array(
      '@successCount' => $successCount,
      '@totalCount' => count($results),
    )));

    foreach ($results as $result) {
      drupal_set_message($result['message'], $result['type']);
    }
  }
  else {
    // An error occurred.
    // $operations contains the operations that remained unprocessed.
    $error_operation = reset($operations);

    ValghallaLogger::error(sprintf('Error occurred during push operation. Error: %s', print_r($error_operation[0], TRUE)));
    drupal_set_message(
      t('An error occurred while processing {
      @operation
} with arguments : @args',
        array(
          '@operation' => $error_operation[0],
          '@args' => print_r($error_operation[0], TRUE),
        )
      ),
      'error'
    );
  }

  // Updating last push timestamp.
  variable_set('valgalla_internal_server_last_push_timestamp', time());
}
